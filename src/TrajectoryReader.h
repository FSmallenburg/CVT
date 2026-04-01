#pragma once

#include "ParticleSystem.h"
#include "SimulationBox.h"

#include <cstddef>
#include <string>
#include <vector>

class TrajectoryReader
{
  public:
    enum class FileType
    {
        Sphere,
        Rod,
      Cube,
        Patchy,
        Patchy2D,
    };

    explicit TrajectoryReader(std::string path);

    bool isOpen() const;
    const std::string &error() const;
    size_t frameCount() const;
    FileType fileType() const;

    bool loadFrame(size_t frameIndex, ParticleSystem &particleSystem,
                   SimulationBox &simulationBox) const;

  private:
    bool scanFrames();

    std::string m_path;
    mutable std::string m_error;
    std::vector<std::streamoff> m_frameOffsets;
    FileType m_fileType = FileType::Sphere;
};