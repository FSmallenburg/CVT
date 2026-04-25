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

namespace
{

constexpr float kSphericalBoxPadding = 1.0f;
constexpr float kLegacyPatchyCoreRadius = 0.5f;
constexpr float kLegacyPatchyInteractionRange = 1.1f;

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
                      + ". Expected one of: .sph, .bsph, .osph, .dsk, .rod, .cub, .gon, .voro, .ptc, .pat, .patch";
        }
        else
        {
            m_error = "Unsupported trajectory file extension '" + extension + "' in "
                      + m_path + ". Expected one of: .sph, .bsph, .osph, .dsk, .rod, .cub, .gon, .voro, .ptc, .pat, .patch";
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