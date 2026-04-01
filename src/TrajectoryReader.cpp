#include "ColorPalette.h"
#include "PatchPlacement.h"
#include "TrajectoryReader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace
{

constexpr float kSphericalBoxPadding = 1.0f;

TrajectoryReader::FileType detectFileType(const std::string &path)
{
    const size_t extensionPos = path.find_last_of('.');
    if (extensionPos == std::string::npos)
    {
        return TrajectoryReader::FileType::Sphere;
    }

    std::string extension = path.substr(extensionPos);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (extension == ".rod")
    {
        return TrajectoryReader::FileType::Rod;
    }
    if (extension == ".ptc")
    {
        return TrajectoryReader::FileType::Patchy;
    }

    return TrajectoryReader::FileType::Sphere;
}

bool readNextDataLine(std::istream &input, std::string &line, std::streamoff *offset = nullptr)
{
    while (true)
    {
        std::streamoff lineOffset = static_cast<std::streamoff>(input.tellg());
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

} // namespace

TrajectoryReader::TrajectoryReader(std::string path) : m_path(std::move(path))
{
    m_fileType = detectFileType(m_path);
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

TrajectoryReader::FileType TrajectoryReader::fileType() const
{
    return m_fileType;
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

    std::ifstream input(m_path);
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
        else if (m_fileType == FileType::Patchy)
        {
            std::vector<std::string> tokens;
            std::string token;
            while (particleStream >> token)
            {
                tokens.push_back(token);
            }

            if (tokens.size() < 13u)
            {
                return setParticleError(
                    "patchy particles require radius, cosHalfAngle, capDiameter, 9 rotation values, and at least one bond id");
            }

            PatchyParticleData patchData;
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

            std::array<float, 9> parsedOrientationMatrix{};
            for (size_t matrixIndex = 0; matrixIndex < 9u; ++matrixIndex)
            {
                if (!parseFloatToken(tokens[3u + matrixIndex],
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

            patchData.bondIds.reserve(tokens.size() - 12u);
            for (size_t tokenIndex = 12u; tokenIndex < tokens.size(); ++tokenIndex)
            {
                int32_t bondId = -1;
                if (!parseIntToken(tokens[tokenIndex], bondId))
                {
                    return setParticleError("invalid bond id token '"
                                            + tokens[tokenIndex] + "'");
                }
                patchData.bondIds.push_back(bondId);
            }

            if (!hasPatchPlacement(patchData.bondIds.size()))
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
        }

        particleSystem.addParticle(particle);
    }

    m_error.clear();
    return true;
}

bool TrajectoryReader::scanFrames()
{
    std::ifstream input(m_path);
    if (!input)
    {
        m_error = "Failed to open trajectory file: " + m_path;
        return false;
    }

    std::string line;
    std::streamoff frameOffset = 0;
    while (readNextDataLine(input, line, &frameOffset))
    {
        size_t particleCount = 0;
        if (!parseFrameHeader(line, particleCount))
        {
            m_error = "Invalid frame header in trajectory file: " + m_path;
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