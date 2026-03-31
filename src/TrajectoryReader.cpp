#include "ColorPalette.h"
#include "TrajectoryReader.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <algorithm>

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
    if (frameIndex >= m_frameOffsets.size())
    {
        return false;
    }

    std::ifstream input(m_path);
    if (!input)
    {
        return false;
    }
    input.seekg(m_frameOffsets[frameIndex]);

    std::string line;
    if (!readNextDataLine(input, line))
    {
        return false;
    }

    size_t particleCount = 0;
    if (!parseFrameHeader(line, particleCount))
    {
        return false;
    }

    if (!readNextDataLine(input, line))
    {
        return false;
    }
    std::istringstream boxStream(line);
    float boxX = 0.0f;
    float boxY = 0.0f;
    float boxZ = 0.0f;
    if (!(boxStream >> boxX >> boxY >> boxZ))
    {
        if (!parseBallBounds(line, simulationBox))
        {
            return false;
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
        if (!readNextDataLine(input, line))
        {
            return false;
        }

        std::istringstream particleStream(line);
        char label = 'A';
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (!(particleStream >> label >> x >> y >> z))
        {
            return false;
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
                return false;
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
        else
        {
            float radius = 1.0f;
            if (!(particleStream >> radius))
            {
                return false;
            }
            particle.setUniformScale(radius);
        }

        particleSystem.addParticle(particle);
    }

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