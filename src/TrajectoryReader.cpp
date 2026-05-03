#include "ColorPalette.h"
#include "BxVec3Operators.h"
#include "PatchPlacement.h"
#include "TrajectoryReader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace
{

constexpr float kSphericalBoxPadding = 1.0f;
constexpr float kLegacyPatchyCoreRadius = 0.5f;
constexpr float kLegacyPatchyInteractionRange = 1.1f;
constexpr float kLammpsDefaultRadius = 0.5f;
constexpr float kLammpsDimensionalityEpsilon = 1.0e-5f;

enum class LammpsCoordinateMode
{
    Cartesian,
    Scaled,
};

struct LammpsAtomColumns
{
    static constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();

    size_t idIndex = kInvalidIndex;
    size_t typeIndex = kInvalidIndex;
    size_t xIndex = kInvalidIndex;
    size_t yIndex = kInvalidIndex;
    size_t zIndex = kInvalidIndex;
    size_t radiusIndex = kInvalidIndex;
    bool radiusIsDiameter = false;
    LammpsCoordinateMode coordinateMode = LammpsCoordinateMode::Cartesian;

    bool hasRequiredColumns() const
    {
        return idIndex != kInvalidIndex
               && typeIndex != kInvalidIndex
               && xIndex != kInvalidIndex
               && yIndex != kInvalidIndex
               && zIndex != kInvalidIndex;
    }
};

struct LammpsBoundsData
{
    bx::Vec3 minBounds{0.0f, 0.0f, 0.0f};
    bx::Vec3 maxBounds{0.0f, 0.0f, 0.0f};
    std::array<bool, 3> periodic{true, true, true};
    bool isTriclinic = false;
    bx::Vec3 cellOrigin{0.0f, 0.0f, 0.0f};
    std::array<bx::Vec3, 3> cellVectors{
        bx::Vec3{0.0f, 0.0f, 0.0f},
        bx::Vec3{0.0f, 0.0f, 0.0f},
        bx::Vec3{0.0f, 0.0f, 0.0f},
    };
};

std::string lowercaseExtension(const std::string &path)
{
    const size_t extensionPos = path.find_last_of('.');
    if (extensionPos == std::string::npos)
    {
        return {};
    }

    std::string extension = path.substr(extensionPos);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension;
}

std::optional<TrajectoryReader::FileType> detectFileType(const std::string &path)
{
    const std::string extension = lowercaseExtension(path);
    if (extension == ".sph")
    {
        return TrajectoryReader::FileType::Sphere;
    }
    if (extension == ".bsph")
    {
        return TrajectoryReader::FileType::BondedSphere;
    }
    if (extension == ".osph")
    {
        return TrajectoryReader::FileType::OrderedSphere;
    }
    if (extension == ".lammpstrj")
    {
        return TrajectoryReader::FileType::LammpsTrajectory;
    }
    if (extension == ".dsk")
    {
        return TrajectoryReader::FileType::Disk;
    }
    if (extension == ".rod")
    {
        return TrajectoryReader::FileType::Rod;
    }
    if (extension == ".cub")
    {
        return TrajectoryReader::FileType::Cube;
    }
    if (extension == ".gon")
    {
        return TrajectoryReader::FileType::Polygon;
    }
    if (extension == ".voro")
    {
        return TrajectoryReader::FileType::Voronoi;
    }
    if (extension == ".ptc")
    {
        return TrajectoryReader::FileType::Patchy;
    }
    if (extension == ".pat")
    {
        return TrajectoryReader::FileType::PatchyLegacy;
    }
    if (extension == ".patch")
    {
        return TrajectoryReader::FileType::Patchy2D;
    }

    return std::nullopt;
}

TrajectoryReader::Dimensionality detectDimensionality(TrajectoryReader::FileType fileType)
{
    switch (fileType)
    {
    case TrajectoryReader::FileType::Disk:
    case TrajectoryReader::FileType::Polygon:
    case TrajectoryReader::FileType::Patchy2D:
        return TrajectoryReader::Dimensionality::TwoDimensional;
    case TrajectoryReader::FileType::Sphere:
    case TrajectoryReader::FileType::BondedSphere:
    case TrajectoryReader::FileType::OrderedSphere:
    case TrajectoryReader::FileType::LammpsTrajectory:
    case TrajectoryReader::FileType::Rod:
    case TrajectoryReader::FileType::Cube:
    case TrajectoryReader::FileType::Voronoi:
    case TrajectoryReader::FileType::Patchy:
    case TrajectoryReader::FileType::PatchyLegacy:
        return TrajectoryReader::Dimensionality::ThreeDimensional;
    }

    return TrajectoryReader::Dimensionality::ThreeDimensional;
}

bool readNextDataLine(std::istream &input, std::string &line, std::streampos *offset = nullptr)
{
    while (true)
    {
        const std::streampos lineOffset = input.tellg();
        if (!std::getline(input, line))
        {
            return false;
        }

        size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos)
        {
            continue;
        }
        if (line[first] == '#')
        {
            continue;
        }

        if (offset != nullptr)
        {
            *offset = lineOffset;
        }
        return true;
    }
}

bool parseBallBounds(const std::string &line, SimulationBox &simulationBox)
{
    std::istringstream ballStream(line);
    std::string keyword;
    float originalRadius = 0.0f;
    float currentRadius = 0.0f;
    if (!(ballStream >> keyword) || keyword != "ball")
    {
        return false;
    }

    if (!(ballStream >> originalRadius))
    {
        return false;
    }

    if (!(ballStream >> currentRadius))
    {
        currentRadius = originalRadius;
    }

    const float boundsRadius = originalRadius + 0.5f * kSphericalBoxPadding;
    simulationBox.setSphericalBounds({0.0f, 0.0f, 0.0f}, boundsRadius, currentRadius);
    simulationBox.setPeriodic(false, false, false);
    return true;
}

bool parseFrameHeader(const std::string &line, size_t &particleCount)
{
    std::istringstream headerStream(line);
    if (headerStream >> particleCount)
    {
        return true;
    }

    headerStream.clear();
    headerStream.str(line);
    char frameMarker = '\0';
    return (headerStream >> frameMarker >> particleCount) && frameMarker == '&';
}

bool parseFloatToken(const std::string &token, float &value)
{
    try
    {
        size_t parsedLength = 0;
        value = std::stof(token, &parsedLength);
        return parsedLength == token.size();
    }
    catch (const std::exception &)
    {
        return false;
    }
}

bool parseIntToken(const std::string &token, int32_t &value)
{
    try
    {
        size_t parsedLength = 0;
        const long parsedValue = std::stol(token, &parsedLength, 10);
        if (parsedLength != token.size()
            || parsedValue < std::numeric_limits<int32_t>::min()
            || parsedValue > std::numeric_limits<int32_t>::max())
        {
            return false;
        }
        value = static_cast<int32_t>(parsedValue);
        return true;
    }
    catch (const std::exception &)
    {
        return false;
    }
}

std::vector<std::string> splitTokens(const std::string &line)
{
    std::istringstream lineStream(line);
    std::vector<std::string> tokens;
    std::string token;
    while (lineStream >> token)
    {
        tokens.push_back(token);
    }
    return tokens;
}

bool startsWith(const std::string &value, const char *prefix)
{
    return value.rfind(prefix, 0u) == 0u;
}

char lammpsTypeLabel(int32_t numericType)
{
    const int32_t clampedType = std::max<int32_t>(numericType, 1);
    return static_cast<char>('A' + ((clampedType - 1) % 26));
}

bool readRequiredDataLine(std::istream &input, std::string &line, std::string &error)
{
    if (!readNextDataLine(input, line))
    {
        error = "unexpected end of file";
        return false;
    }
    return true;
}

bool parseLammpsAtomCountHeader(const std::string &line)
{
    return startsWith(line, "ITEM: NUMBER OF ATOMS");
}

bool parseLammpsAtomsHeader(const std::string &line,
                            LammpsAtomColumns &columns,
                            std::string &error)
{
    const std::vector<std::string> tokens = splitTokens(line);
    if (tokens.size() < 5u || tokens[0] != "ITEM:" || tokens[1] != "ATOMS")
    {
        error = "expected 'ITEM: ATOMS ...' header";
        return false;
    }

    columns = {};
    const auto setCoordinateColumns = [&](const char *xName,
                                          const char *yName,
                                          const char *zName,
                                          LammpsCoordinateMode mode) {
        size_t xIndex = LammpsAtomColumns::kInvalidIndex;
        size_t yIndex = LammpsAtomColumns::kInvalidIndex;
        size_t zIndex = LammpsAtomColumns::kInvalidIndex;
        for (size_t tokenIndex = 2u; tokenIndex < tokens.size(); ++tokenIndex)
        {
            const std::string &name = tokens[tokenIndex];
            const size_t columnIndex = tokenIndex - 2u;
            if (name == xName)
            {
                xIndex = columnIndex;
            }
            else if (name == yName)
            {
                yIndex = columnIndex;
            }
            else if (name == zName)
            {
                zIndex = columnIndex;
            }
        }

        if (xIndex != LammpsAtomColumns::kInvalidIndex
            && yIndex != LammpsAtomColumns::kInvalidIndex
            && zIndex != LammpsAtomColumns::kInvalidIndex)
        {
            columns.xIndex = xIndex;
            columns.yIndex = yIndex;
            columns.zIndex = zIndex;
            columns.coordinateMode = mode;
            return true;
        }

        return false;
    };

    for (size_t tokenIndex = 2u; tokenIndex < tokens.size(); ++tokenIndex)
    {
        const std::string &name = tokens[tokenIndex];
        const size_t columnIndex = tokenIndex - 2u;
        if (name == "id")
        {
            columns.idIndex = columnIndex;
        }
        else if (name == "type")
        {
            columns.typeIndex = columnIndex;
        }
        else if (name == "radius")
        {
            columns.radiusIndex = columnIndex;
            columns.radiusIsDiameter = false;
        }
        else if (name == "diameter")
        {
            columns.radiusIndex = columnIndex;
            columns.radiusIsDiameter = true;
        }
    }

    if (!setCoordinateColumns("x", "y", "z", LammpsCoordinateMode::Cartesian)
        && !setCoordinateColumns("xu", "yu", "zu", LammpsCoordinateMode::Cartesian)
        && !setCoordinateColumns("xs", "ys", "zs", LammpsCoordinateMode::Scaled)
        && !setCoordinateColumns("xsu", "ysu", "zsu", LammpsCoordinateMode::Scaled))
    {
        error = "LAMMPS dump must provide one of: x/y/z, xu/yu/zu, xs/ys/zs, or xsu/ysu/zsu columns";
        return false;
    }

    if (!columns.hasRequiredColumns())
    {
        error = "LAMMPS dump requires id, type, and position columns";
        return false;
    }

    return true;
}

bool parseLammpsBoundsHeader(const std::string &line,
                             std::array<bool, 3> &periodic,
                             bool &hasTilt,
                             std::string &error)
{
    const std::vector<std::string> tokens = splitTokens(line);
    if (tokens.size() < 5u || tokens[0] != "ITEM:" || tokens[1] != "BOX" || tokens[2] != "BOUNDS")
    {
        error = "expected 'ITEM: BOX BOUNDS ...' header";
        return false;
    }

    periodic = {true, true, true};
    hasTilt = false;
    size_t boundaryAxis = 0u;
    for (size_t tokenIndex = 3u; tokenIndex < tokens.size(); ++tokenIndex)
    {
        const std::string &token = tokens[tokenIndex];
        if (token == "xy" || token == "xz" || token == "yz")
        {
            hasTilt = true;
            continue;
        }

        if (token.size() == 2u && boundaryAxis < periodic.size())
        {
            periodic[boundaryAxis] = token.find('p') != std::string::npos;
            ++boundaryAxis;
        }
    }

    return true;
}

bool parseLammpsBoundsSection(std::istream &input,
                              const std::string &headerLine,
                              LammpsBoundsData &bounds,
                              std::string &error)
{
    bool hasTilt = false;
    if (!parseLammpsBoundsHeader(headerLine, bounds.periodic, hasTilt, error))
    {
        return false;
    }

    std::array<float, 3> minBounds{0.0f, 0.0f, 0.0f};
    std::array<float, 3> maxBounds{0.0f, 0.0f, 0.0f};
    std::array<float, 3> tiltFactors{0.0f, 0.0f, 0.0f};
    for (size_t axis = 0u; axis < 3u; ++axis)
    {
        std::string line;
        if (!readRequiredDataLine(input, line, error))
        {
            error = "missing LAMMPS bounds line";
            return false;
        }

        std::istringstream boundsStream(line);
        if (!(boundsStream >> minBounds[axis] >> maxBounds[axis]))
        {
            error = "invalid LAMMPS bounds line: '" + line + "'";
            return false;
        }

        float tiltValue = 0.0f;
        if (boundsStream >> tiltValue)
        {
            tiltFactors[axis] = tiltValue;
        }
        else if (hasTilt)
        {
            error = "expected tilt factor in triclinic LAMMPS bounds line: '" + line + "'";
            return false;
        }
    }

    if (!hasTilt)
    {
        bounds.minBounds = {minBounds[0], minBounds[1], minBounds[2]};
        bounds.maxBounds = {maxBounds[0], maxBounds[1], maxBounds[2]};
        bounds.cellOrigin = bounds.minBounds;
        bounds.cellVectors = {
            bx::Vec3{bounds.maxBounds.x - bounds.minBounds.x, 0.0f, 0.0f},
            bx::Vec3{0.0f, bounds.maxBounds.y - bounds.minBounds.y, 0.0f},
            bx::Vec3{0.0f, 0.0f, bounds.maxBounds.z - bounds.minBounds.z},
        };
        return true;
    }

    bounds.isTriclinic = true;
    const float xy = tiltFactors[0];
    const float xz = tiltFactors[1];
    const float yz = tiltFactors[2];

    const float xlo = minBounds[0] - std::min({0.0f, xy, xz, xy + xz});
    const float xhi = maxBounds[0] - std::max({0.0f, xy, xz, xy + xz});
    const float ylo = minBounds[1] - std::min(0.0f, yz);
    const float yhi = maxBounds[1] - std::max(0.0f, yz);
    const float zlo = minBounds[2];
    const float zhi = maxBounds[2];

    const float lx = xhi - xlo;
    const float ly = yhi - ylo;
    const float lz = zhi - zlo;
    if (!(lx > 0.0f) || !(ly > 0.0f) || !(lz > 0.0f))
    {
        error = "invalid triclinic LAMMPS bounds with non-positive cell length";
        return false;
    }

    bounds.cellOrigin = {xlo, ylo, zlo};
    bounds.cellVectors = {
        bx::Vec3{lx, 0.0f, 0.0f},
        bx::Vec3{xy, ly, 0.0f},
        bx::Vec3{xz, yz, lz},
    };

    const std::array<bx::Vec3, 8> corners = {
        bounds.cellOrigin,
        bounds.cellOrigin + bounds.cellVectors[0],
        bounds.cellOrigin + bounds.cellVectors[0] + bounds.cellVectors[1],
        bounds.cellOrigin + bounds.cellVectors[1],
        bounds.cellOrigin + bounds.cellVectors[2],
        bounds.cellOrigin + bounds.cellVectors[0] + bounds.cellVectors[2],
        bounds.cellOrigin + bounds.cellVectors[0] + bounds.cellVectors[1] + bounds.cellVectors[2],
        bounds.cellOrigin + bounds.cellVectors[1] + bounds.cellVectors[2],
    };
    bounds.minBounds = corners[0];
    bounds.maxBounds = corners[0];
    for (const bx::Vec3 &corner : corners)
    {
        bounds.minBounds.x = bx::min(bounds.minBounds.x, corner.x);
        bounds.minBounds.y = bx::min(bounds.minBounds.y, corner.y);
        bounds.minBounds.z = bx::min(bounds.minBounds.z, corner.z);
        bounds.maxBounds.x = bx::max(bounds.maxBounds.x, corner.x);
        bounds.maxBounds.y = bx::max(bounds.maxBounds.y, corner.y);
        bounds.maxBounds.z = bx::max(bounds.maxBounds.z, corner.z);
    }

    return true;
}

float lammpsCoordinateToPosition(float value,
                                 float minBound,
                                 float maxBound,
                                 LammpsCoordinateMode mode)
{
    if (mode == LammpsCoordinateMode::Scaled)
    {
        return bx::lerp(minBound, maxBound, value);
    }

    return value;
}

bool parseLammpsParticleLine(const std::string &line,
                             const LammpsAtomColumns &columns,
                             const LammpsBoundsData &bounds,
                             Particle &particle,
                             std::string &error)
{
    const std::vector<std::string> tokens = splitTokens(line);
    const size_t minimumTokenCount = std::max({columns.idIndex,
                                               columns.typeIndex,
                                               columns.xIndex,
                                               columns.yIndex,
                                               columns.zIndex,
                                               columns.radiusIndex})
                                     + 1u;
    if (tokens.size() < minimumTokenCount)
    {
        error = "particle line has too few columns: '" + line + "'";
        return false;
    }

    int32_t id = 0;
    int32_t numericType = 0;
    float rawX = 0.0f;
    float rawY = 0.0f;
    float rawZ = 0.0f;
    if (!parseIntToken(tokens[columns.idIndex], id)
        || !parseIntToken(tokens[columns.typeIndex], numericType)
        || !parseFloatToken(tokens[columns.xIndex], rawX)
        || !parseFloatToken(tokens[columns.yIndex], rawY)
        || !parseFloatToken(tokens[columns.zIndex], rawZ))
    {
        error = "invalid id, type, or coordinate value in line '" + line + "'";
        return false;
    }
    if (id < 0)
    {
        error = "LAMMPS atom ids must be non-negative";
        return false;
    }

    float radius = kLammpsDefaultRadius;
    if (columns.radiusIndex != LammpsAtomColumns::kInvalidIndex)
    {
        if (!parseFloatToken(tokens[columns.radiusIndex], radius))
        {
            error = "invalid LAMMPS radius value in line '" + line + "'";
            return false;
        }
        if (columns.radiusIsDiameter)
        {
            radius *= 0.5f;
        }
    }

    particle.id = static_cast<uint32_t>(id);
    particle.typeLabel = lammpsTypeLabel(numericType);
    particle.position = {
        lammpsCoordinateToPosition(rawX, bounds.minBounds.x, bounds.maxBounds.x,
                                   columns.coordinateMode),
        lammpsCoordinateToPosition(rawY, bounds.minBounds.y, bounds.maxBounds.y,
                                   columns.coordinateMode),
        lammpsCoordinateToPosition(rawZ, bounds.minBounds.z, bounds.maxBounds.z,
                                   columns.coordinateMode),
    };
    particle.baseColor = colorFromLetter(particle.typeLabel);
    particle.color = particle.baseColor;
    particle.setUniformScale(radius);
    particle.sizeParams[3] = 1.0f;
    return true;
}

bool orthonormalizeRotationMatrix(std::array<float, 9> &matrix)
{
    bx::Vec3 x{matrix[0], matrix[1], matrix[2]};
    bx::Vec3 y{matrix[3], matrix[4], matrix[5]};

    const float xLength = bx::length(x);
    const float yLength = bx::length(y);
    if (xLength <= 1.0e-6f || yLength <= 1.0e-6f)
    {
        return false;
    }

    x *= 1.0f / xLength;

    // Remove x component from y before normalization (Gram-Schmidt).
    y -= x * bx::dot(y, x);
    const float yOrthoLength = bx::length(y);
    if (yOrthoLength <= 1.0e-6f)
    {
        return false;
    }
    y *= 1.0f / yOrthoLength;

    bx::Vec3 z = bx::cross(x, y);
    const float zLength = bx::length(z);
    if (zLength <= 1.0e-6f)
    {
        return false;
    }
    z *= 1.0f / zLength;

    // Recompute y so x,y,z are exactly orthonormal and right-handed.
    y = bx::cross(z, x);

    matrix = {
        x.x, x.y, x.z,
        y.x, y.y, y.z,
        z.x, z.y, z.z,
    };

    return true;
}

bool parseVoronoiPointSets(const std::string &trajectoryPath,
                          std::vector<std::vector<bx::Vec3>> &outPointSets,
                          std::string &error)
{
    outPointSets.clear();

    const std::filesystem::path sourcePath(trajectoryPath);
    const std::filesystem::path pointSetPath = sourcePath.parent_path() / "voropoints.dat";
    std::ifstream pointSetInput(pointSetPath);
    if (!pointSetInput)
    {
        error = "Missing voropoints.dat next to trajectory file: " + pointSetPath.string();
        return false;
    }

    std::string line;
    while (readNextDataLine(pointSetInput, line))
    {
        std::istringstream countStream(line);
        size_t pointCount = 0u;
        if (!(countStream >> pointCount) || pointCount == 0u)
        {
            error = "Invalid point-set size line in " + pointSetPath.string() + ": '" + line
                    + "'";
            return false;
        }

        std::vector<bx::Vec3> pointSet;
        pointSet.reserve(pointCount);
        for (size_t pointIndex = 0u; pointIndex < pointCount; ++pointIndex)
        {
            if (!readNextDataLine(pointSetInput, line))
            {
                error = "Unexpected end of " + pointSetPath.string()
                        + " while reading point set with " + std::to_string(pointCount)
                        + " points";
                return false;
            }

            std::istringstream pointStream(line);
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (!(pointStream >> x >> y >> z))
            {
                error = "Invalid Voronoi point line in " + pointSetPath.string() + ": '"
                        + line + "'";
                return false;
            }

            pointSet.push_back({x, y, z});
        }

        outPointSets.push_back(std::move(pointSet));
    }

    if (outPointSets.empty())
    {
        error = "No Voronoi point sets found in " + pointSetPath.string();
        return false;
    }

    return true;
}

size_t voronoiShapeIndexForLabel(char label, size_t shapeCount)
{
    const unsigned char rawLabel = static_cast<unsigned char>(label);
    if (std::isalpha(rawLabel) != 0)
    {
        const int alphaIndex = std::toupper(rawLabel) - 'A';
        return static_cast<size_t>(std::max(alphaIndex, 0)) % shapeCount;
    }

    return static_cast<size_t>(std::toupper(rawLabel)) % shapeCount;
}

} // namespace

TrajectoryReader::TrajectoryReader(std::string path) : m_path(std::move(path))
{
    const std::optional<FileType> detectedFileType = detectFileType(m_path);
    if (!detectedFileType.has_value())
    {
        const std::string extension = lowercaseExtension(m_path);
        if (extension.empty())
        {
            m_error = "Unsupported trajectory file extension in " + m_path
                      + ". Expected one of: .sph, .bsph, .osph, .lammpstrj, .dsk, .rod, .cub, .gon, .voro, .ptc, .pat, .patch";
        }
        else
        {
            m_error = "Unsupported trajectory file extension '" + extension + "' in "
                      + m_path + ". Expected one of: .sph, .bsph, .osph, .lammpstrj, .dsk, .rod, .cub, .gon, .voro, .ptc, .pat, .patch";
        }
        return;
    }

    m_fileType = *detectedFileType;
    m_dimensionality = detectDimensionality(m_fileType);

    if (m_fileType == FileType::Voronoi)
    {
        if (!parseVoronoiPointSets(m_path, m_voronoiPointSets, m_error))
        {
            return;
        }
    }

    scanFrames();
}

bool TrajectoryReader::isOpen() const
{
    return m_error.empty();
}

const std::string &TrajectoryReader::error() const
{
    return m_error;
}

size_t TrajectoryReader::frameCount() const
{
    return m_frameOffsets.size();
}

bx::Vec3 TrajectoryReader::maxFrameBoxSize() const
{
    return m_maxFrameBoxSize;
}

TrajectoryReader::FileType TrajectoryReader::fileType() const
{
    return m_fileType;
}

TrajectoryReader::Dimensionality TrajectoryReader::dimensionality() const
{
    return m_dimensionality;
}

const std::vector<std::vector<bx::Vec3>> &TrajectoryReader::voronoiPointSets() const
{
    return m_voronoiPointSets;
}

bool TrajectoryReader::loadFrame(size_t frameIndex, ParticleSystem &particleSystem,
                                 SimulationBox &simulationBox) const
{
    m_error.clear();

    const auto setParseError = [this, frameIndex](const std::string &message) {
        std::ostringstream errorStream;
        errorStream << "Frame " << frameIndex << " parse error in " << m_path << ": "
                    << message;
        m_error = errorStream.str();
        return false;
    };

    if (frameIndex >= m_frameOffsets.size())
    {
        std::ostringstream errorStream;
        errorStream << "Frame index " << frameIndex << " is out of range for " << m_path;
        m_error = errorStream.str();
        return false;
    }

    std::ifstream input(m_path, std::ios::binary);
    if (!input)
    {
        m_error = "Failed to open trajectory file: " + m_path;
        return false;
    }
    input.seekg(m_frameOffsets[frameIndex]);

    std::string line;
    if (!readNextDataLine(input, line))
    {
        return setParseError("missing frame header");
    }

    if (m_fileType == FileType::LammpsTrajectory)
    {
        if (!startsWith(line, "ITEM: TIMESTEP"))
        {
            return setParseError("expected 'ITEM: TIMESTEP' header, got '" + line + "'");
        }

        std::string localError;
        if (!readRequiredDataLine(input, line, localError))
        {
            return setParseError("missing timestep value");
        }

        if (!readRequiredDataLine(input, line, localError)
            || !parseLammpsAtomCountHeader(line))
        {
            return setParseError("missing 'ITEM: NUMBER OF ATOMS' header");
        }

        if (!readRequiredDataLine(input, line, localError))
        {
            return setParseError("missing atom count value");
        }

        size_t particleCount = 0u;
        if (!parseFrameHeader(line, particleCount))
        {
            return setParseError("invalid atom count line: '" + line + "'");
        }

        if (!readRequiredDataLine(input, line, localError))
        {
            return setParseError("missing BOX BOUNDS header");
        }

        LammpsBoundsData bounds;
        if (!parseLammpsBoundsSection(input, line, bounds, localError))
        {
            return setParseError(localError);
        }
        if (bounds.isTriclinic)
        {
            simulationBox.setTriclinicBounds(bounds.cellOrigin,
                                             bounds.cellVectors[0],
                                             bounds.cellVectors[1],
                                             bounds.cellVectors[2]);
        }
        else
        {
            simulationBox.setBounds(bounds.minBounds, bounds.maxBounds);
        }
        simulationBox.setPeriodic(bounds.periodic[0], bounds.periodic[1], bounds.periodic[2]);

        if (!readRequiredDataLine(input, line, localError))
        {
            return setParseError("missing ATOMS header");
        }

        LammpsAtomColumns columns;
        if (!parseLammpsAtomsHeader(line, columns, localError))
        {
            return setParseError(localError);
        }

        particleSystem.clear();
        particleSystem.reserve(particleCount);
        std::unordered_set<uint32_t> seenIds;
        seenIds.reserve(particleCount);
        for (size_t particleIndex = 0u; particleIndex < particleCount; ++particleIndex)
        {
            if (!readRequiredDataLine(input, line, localError))
            {
                return setParseError("missing particle line");
            }

            Particle particle;
            if (!parseLammpsParticleLine(line, columns, bounds, particle, localError))
            {
                std::ostringstream message;
                message << "particle " << (particleIndex + 1u) << ": " << localError;
                return setParseError(message.str());
            }

            if (!seenIds.insert(particle.id).second)
            {
                std::ostringstream message;
                message << "particle " << (particleIndex + 1u)
                        << ": duplicate LAMMPS atom id " << particle.id;
                return setParseError(message.str());
            }

            particleSystem.addParticle(particle);
        }

        m_error.clear();
        return true;
    }

    size_t particleCount = 0;
    if (!parseFrameHeader(line, particleCount))
    {
        return setParseError("invalid frame header: '" + line + "'");
    }

    if (!readNextDataLine(input, line))
    {
        return setParseError("missing box line");
    }
    std::istringstream boxStream(line);
    float boxX = 0.0f;
    float boxY = 0.0f;
    float boxZ = 0.0f;
    if (!(boxStream >> boxX >> boxY >> boxZ))
    {
        if (!parseBallBounds(line, simulationBox))
        {
            return setParseError("invalid box line: '" + line + "'");
        }
    }
    else
    {
        simulationBox.setBounds({0.0f, 0.0f, 0.0f}, {boxX, boxY, boxZ});
        simulationBox.setPeriodic(true, true, true);
    }

    particleSystem.clear();
    particleSystem.reserve(particleCount);

    std::optional<size_t> expectedOrderParameterCount;

    for (size_t particleIndex = 0; particleIndex < particleCount; ++particleIndex)
    {
        const auto setParticleError = [&](const std::string &message) {
            std::ostringstream errorStream;
            errorStream << "particle " << (particleIndex + 1u) << ": " << message;
            return setParseError(errorStream.str());
        };

        if (!readNextDataLine(input, line))
        {
            return setParticleError("missing particle line");
        }

        std::istringstream particleStream(line);
        char label = 'A';
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (!(particleStream >> label >> x >> y >> z))
        {
            return setParticleError("expected label and xyz coordinates in line '" + line
                                    + "'");
        }

        Particle particle;
        particle.typeLabel = label;
        particle.id = static_cast<uint32_t>(particleIndex + 1);
        particle.position = {x, y, z};
        particle.baseColor = colorFromLetter(label);
        particle.color = particle.baseColor;

        if (m_fileType == FileType::Rod)
        {
            std::vector<float> values;
            float value = 0.0f;
            while (particleStream >> value)
            {
                values.push_back(value);
            }

            if (values.size() != 3u && values.size() != 4u)
            {
                return setParticleError("rod particles require 3 or 4 trailing numeric fields");
            }

            const float radius = values.size() == 4u ? values[0] * 0.5f : 0.5f;
            const size_t directionOffset = values.size() == 4u ? 1u : 0u;
            const bx::Vec3 direction = {values[directionOffset], values[directionOffset + 1u],
                                        values[directionOffset + 2u]};
            particle.direction = direction;
            particle.sizeParams[0] = radius;
            particle.sizeParams[1] = bx::length(direction);
            particle.sizeParams[2] = radius;
            particle.sizeParams[3] = 1.0f;
        }
        else if (m_fileType == FileType::Cube)
        {
            std::vector<std::string> tokens;
            std::string token;
            while (particleStream >> token)
            {
                tokens.push_back(token);
            }

            if (tokens.size() != 10u)
            {
                return setParticleError(
                    "cube particles require edge length followed by 9 rotation matrix values");
            }

            float edgeLength = 0.0f;
            if (!parseFloatToken(tokens[0], edgeLength))
            {
                return setParticleError("invalid cube edge length");
            }

            std::array<float, 9> parsedOrientationMatrix{};
            for (size_t matrixIndex = 0; matrixIndex < 9u; ++matrixIndex)
            {
                if (!parseFloatToken(tokens[1u + matrixIndex],
                                     parsedOrientationMatrix[matrixIndex]))
                {
                    return setParticleError("invalid rotation matrix entry at index "
                                            + std::to_string(matrixIndex));
                }
            }

            if (!orthonormalizeRotationMatrix(parsedOrientationMatrix))
            {
                return setParticleError("invalid Voronoi rotation matrix (degenerate)");
            }

            for (size_t row = 0; row < 3u; ++row)
            {
                for (size_t column = 0; column < 3u; ++column)
                {
                    particle.orientationMatrix[row * 3u + column] =
                        parsedOrientationMatrix[column * 3u + row];
                }
            }

            particle.hasOrientationMatrix = true;
            particle.setUniformScale(edgeLength);
            particle.sizeParams[3] = 1.0f;
        }
        else if (m_fileType == FileType::Polygon)
        {
            std::vector<std::string> tokens;
            std::string token;
            while (particleStream >> token)
            {
                tokens.push_back(token);
            }

            if (tokens.size() != 3u)
            {
                return setParticleError(
                    "polygon particles require radius, side count, and angle");
            }

            float radius = 0.0f;
            int32_t sideCount = 0;
            float angle = 0.0f;
            if (!parseFloatToken(tokens[0], radius)
                || !parseIntToken(tokens[1], sideCount)
                || !parseFloatToken(tokens[2], angle))
            {
                return setParticleError("invalid polygon radius, side count, or angle");
            }

            if (sideCount < 3 || sideCount > std::numeric_limits<uint16_t>::max())
            {
                return setParticleError("polygon side count must be between 3 and 65535");
            }

            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            particle.direction = {cosine, sine, 0.0f};
            particle.orientationMatrix = {cosine, -sine, 0.0f,
                                          sine, cosine, 0.0f,
                                          0.0f, 0.0f, 1.0f};
            particle.hasOrientationMatrix = true;
            particle.sizeParams[0] = radius;
            particle.sizeParams[1] = static_cast<float>(sideCount);
            particle.sizeParams[2] = 1.0f;
            particle.sizeParams[3] = 1.0f;
        }
        else if (m_fileType == FileType::Voronoi)
        {
            std::vector<std::string> tokens;
            std::string token;
            while (particleStream >> token)
            {
                tokens.push_back(token);
            }

            if (tokens.size() != 10u)
            {
                return setParticleError(
                    "Voronoi particles require size followed by 9 rotation matrix values");
            }

            if (m_voronoiPointSets.empty())
            {
                return setParticleError("no Voronoi point sets loaded");
            }

            float scale = 1.0f;
            if (!parseFloatToken(tokens[0], scale) || scale <= 0.0f)
            {
                return setParticleError("invalid Voronoi particle size");
            }

            std::array<float, 9> parsedOrientationMatrix{};
            for (size_t matrixIndex = 0; matrixIndex < 9u; ++matrixIndex)
            {
                if (!parseFloatToken(tokens[1u + matrixIndex],
                                     parsedOrientationMatrix[matrixIndex]))
                {
                    return setParticleError("invalid rotation matrix entry at index "
                                            + std::to_string(matrixIndex));
                }
            }

            for (size_t row = 0; row < 3u; ++row)
            {
                for (size_t column = 0; column < 3u; ++column)
                {
                    particle.orientationMatrix[row * 3u + column] =
                        parsedOrientationMatrix[column * 3u + row];
                }
            }

            const size_t shapeIndex =
                voronoiShapeIndexForLabel(label, m_voronoiPointSets.size());
            particle.hasOrientationMatrix = true;
            particle.sizeParams[0] = scale;
            particle.sizeParams[1] = scale;
            particle.sizeParams[2] = scale;
            particle.sizeParams[3] = static_cast<float>(shapeIndex);
        }
        else if (m_fileType == FileType::Patchy
                 || m_fileType == FileType::PatchyLegacy
                 || m_fileType == FileType::Patchy2D)
        {
            std::vector<std::string> tokens;
            std::string token;
            while (particleStream >> token)
            {
                tokens.push_back(token);
            }

            const bool isLegacyPatchy = (m_fileType == FileType::PatchyLegacy);
            const size_t minTokenCount = isLegacyPatchy ? 10u : 12u;
            if (tokens.size() < minTokenCount)
            {
                return setParticleError(isLegacyPatchy
                                            ? "legacy patchy particles require cosHalfAngle and 9 rotation matrix values, followed by zero or more bond ids"
                                            : "patchy particles require radius, cosHalfAngle, capDiameter, and 9 rotation matrix values, followed by zero or more bond ids");
            }

            PatchyParticleData patchData;
            patchData.planarPlacement = (m_fileType == FileType::Patchy2D);
            size_t orientationTokenOffset = 0u;
            size_t bondTokenOffset = 0u;
            if (isLegacyPatchy)
            {
                patchData.coreRadius = kLegacyPatchyCoreRadius;
                patchData.capRadius = 0.5f * kLegacyPatchyInteractionRange;
                if (!parseFloatToken(tokens[0], patchData.cosHalfAngle))
                {
                    return setParticleError("invalid cosHalfAngle");
                }
                orientationTokenOffset = 1u;
                bondTokenOffset = 10u;
            }
            else
            {
                if (!parseFloatToken(tokens[0], patchData.coreRadius)
                    || !parseFloatToken(tokens[1], patchData.cosHalfAngle))
                {
                    return setParticleError("invalid core radius or cosHalfAngle");
                }

                float capDiameter = 0.0f;
                if (!parseFloatToken(tokens[2], capDiameter))
                {
                    return setParticleError("invalid cap diameter");
                }
                patchData.capRadius = 0.5f * capDiameter;
                orientationTokenOffset = 3u;
                bondTokenOffset = 12u;
            }

            std::array<float, 9> parsedOrientationMatrix{};
            for (size_t matrixIndex = 0; matrixIndex < 9u; ++matrixIndex)
            {
                if (!parseFloatToken(tokens[orientationTokenOffset + matrixIndex],
                                     parsedOrientationMatrix[matrixIndex]))
                {
                    return setParticleError("invalid rotation matrix entry at index "
                                            + std::to_string(matrixIndex));
                }
            }

            for (size_t row = 0; row < 3u; ++row)
            {
                for (size_t column = 0; column < 3u; ++column)
                {
                    patchData.orientationMatrix[row * 3u + column] =
                        parsedOrientationMatrix[column * 3u + row];
                }
            }

            patchData.bondIds.reserve(tokens.size() - bondTokenOffset);
            for (size_t tokenIndex = bondTokenOffset; tokenIndex < tokens.size(); ++tokenIndex)
            {
                int32_t bondId = -1;
                if (!parseIntToken(tokens[tokenIndex], bondId))
                {
                    return setParticleError("invalid bond id token '"
                                            + tokens[tokenIndex] + "'");
                }
                patchData.bondIds.push_back(bondId);
            }

            if (!patchData.planarPlacement && !hasPatchPlacement(patchData.bondIds.size()))
            {
                return setParticleError("unsupported patch count "
                                        + std::to_string(patchData.bondIds.size()));
            }

            particle.sizeParams[0] = patchData.coreRadius;
            particle.sizeParams[1] = patchData.coreRadius;
            particle.sizeParams[2] = patchData.coreRadius;
            particle.sizeParams[3] = 1.0f;

            particleSystem.addParticle(particle);
            particleSystem.addPatchyMetadata(patchData);
            continue;
        }
        else
        {
            float radius = 1.0f;
            if (!(particleStream >> radius))
            {
                return setParticleError("sphere particles require a radius after xyz");
            }
            particle.setUniformScale(radius);

            if (m_fileType == FileType::OrderedSphere)
            {
                float orderParameter = 0.0f;
                while (particleStream >> orderParameter)
                {
                    particle.orderParameters.push_back(orderParameter);
                }

                if (!particleStream.eof())
                {
                    return setParticleError("invalid order-parameter value in ordered sphere line");
                }

                if (!expectedOrderParameterCount.has_value())
                {
                    expectedOrderParameterCount = particle.orderParameters.size();
                }
                else if (particle.orderParameters.size() != *expectedOrderParameterCount)
                {
                    return setParticleError(
                        "ordered sphere particles must all provide the same number of order parameters");
                }
            }
            else if (m_fileType == FileType::BondedSphere)
            {
                PatchyParticleData patchData;
                int32_t bondId = -1;
                while (particleStream >> bondId)
                {
                    patchData.bondIds.push_back(bondId);
                }

                if (!particleStream.eof())
                {
                    return setParticleError("invalid bonded-sphere bond id token");
                }

                particleSystem.addParticle(particle);
                particleSystem.addPatchyMetadata(patchData);
                continue;
            }
        }

        particleSystem.addParticle(particle);
    }

    m_error.clear();
    return true;
}

bool TrajectoryReader::scanFrames()
{
    std::ifstream input(m_path, std::ios::binary);
    if (!input)
    {
        m_error = "Failed to open trajectory file: " + m_path;
        return false;
    }

    std::string line;
    std::streampos frameOffset = std::streampos(0);

    if (m_fileType == FileType::LammpsTrajectory)
    {
        bool inferredDimensionality = false;
        while (readNextDataLine(input, line, &frameOffset))
        {
            if (!startsWith(line, "ITEM: TIMESTEP"))
            {
                m_error = "Invalid LAMMPS frame header in trajectory file: " + m_path
                          + " : " + line;
                m_frameOffsets.clear();
                return false;
            }

            m_frameOffsets.push_back(frameOffset);

            std::string localError;
            if (!readRequiredDataLine(input, line, localError)
                || !readRequiredDataLine(input, line, localError)
                || !parseLammpsAtomCountHeader(line)
                || !readRequiredDataLine(input, line, localError))
            {
                m_error = "Invalid LAMMPS atom-count section in trajectory file: " + m_path;
                m_frameOffsets.clear();
                return false;
            }

            size_t particleCount = 0u;
            if (!parseFrameHeader(line, particleCount))
            {
                m_error = "Invalid LAMMPS atom count in trajectory file: " + m_path
                          + " : " + line;
                m_frameOffsets.clear();
                return false;
            }

            if (!readRequiredDataLine(input, line, localError))
            {
                m_error = "Missing LAMMPS BOX BOUNDS header in trajectory file: " + m_path;
                m_frameOffsets.clear();
                return false;
            }

            LammpsBoundsData bounds;
            if (!parseLammpsBoundsSection(input, line, bounds, localError))
            {
                m_error = "Invalid LAMMPS bounds in trajectory file: " + m_path + " : "
                          + localError;
                m_frameOffsets.clear();
                return false;
            }

            const bx::Vec3 frameSize = bounds.maxBounds - bounds.minBounds;
            m_maxFrameBoxSize.x = bx::max(m_maxFrameBoxSize.x, frameSize.x);
            m_maxFrameBoxSize.y = bx::max(m_maxFrameBoxSize.y, frameSize.y);
            m_maxFrameBoxSize.z = bx::max(m_maxFrameBoxSize.z, frameSize.z);
            if (!inferredDimensionality)
            {
                m_dimensionality = frameSize.z <= kLammpsDimensionalityEpsilon
                                       ? Dimensionality::TwoDimensional
                                       : Dimensionality::ThreeDimensional;
                inferredDimensionality = true;
            }

            if (!readRequiredDataLine(input, line, localError))
            {
                m_error = "Missing LAMMPS ATOMS header in trajectory file: " + m_path;
                m_frameOffsets.clear();
                return false;
            }

            LammpsAtomColumns columns;
            if (!parseLammpsAtomsHeader(line, columns, localError))
            {
                m_error = "Invalid LAMMPS ATOMS header in trajectory file: " + m_path
                          + " : " + localError;
                m_frameOffsets.clear();
                return false;
            }

            for (size_t particleIndex = 0u; particleIndex < particleCount; ++particleIndex)
            {
                if (!readNextDataLine(input, line))
                {
                    m_error = "Unexpected end of LAMMPS trajectory file while reading particles: "
                              + m_path;
                    m_frameOffsets.clear();
                    return false;
                }
            }
        }

        if (m_frameOffsets.empty())
        {
            m_error = "Trajectory file contains no frames: " + m_path;
            return false;
        }

        return true;
    }

    while (readNextDataLine(input, line, &frameOffset))
    {
        size_t particleCount = 0;
        if (!parseFrameHeader(line, particleCount))
        {
            m_error = "Invalid frame header in trajectory file: " + m_path + " : " + line;
            m_frameOffsets.clear();
            return false;
        }

        m_frameOffsets.push_back(frameOffset);

        if (!readNextDataLine(input, line))
        {
            m_error = "Missing box line in trajectory file: " + m_path;
            m_frameOffsets.clear();
            return false;
        }

        SimulationBox frameBox;
        std::istringstream boxStream(line);
        float boxX = 0.0f;
        float boxY = 0.0f;
        float boxZ = 0.0f;
        if ((boxStream >> boxX >> boxY >> boxZ))
        {
            frameBox.setBounds({0.0f, 0.0f, 0.0f}, {boxX, boxY, boxZ});
        }
        else if (!parseBallBounds(line, frameBox))
        {
            m_error = "Invalid box line in trajectory file: " + m_path + " : " + line;
            m_frameOffsets.clear();
            return false;
        }

        const bx::Vec3 frameSize = frameBox.size();
        m_maxFrameBoxSize.x = bx::max(m_maxFrameBoxSize.x, frameSize.x);
        m_maxFrameBoxSize.y = bx::max(m_maxFrameBoxSize.y, frameSize.y);
        m_maxFrameBoxSize.z = bx::max(m_maxFrameBoxSize.z, frameSize.z);

        for (size_t particleIndex = 0; particleIndex < particleCount; ++particleIndex)
        {
            if (!readNextDataLine(input, line))
            {
                m_error = "Unexpected end of trajectory file while reading particles: " + m_path;
                m_frameOffsets.clear();
                return false;
            }
        }
    }

    if (m_frameOffsets.empty())
    {
        m_error = "Trajectory file contains no frames: " + m_path;
        return false;
    }

    return true;
}