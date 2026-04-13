#pragma once

#include "ParticleSystem.h"
#include "SimulationBox.h"

#include <bx/math.h>

#include <cstddef>
#include <string>
#include <vector>

class TrajectoryReader
{
  public:
    enum class FileType
    {
        Sphere,
        OrderedSphere,
        Disk,
        Rod,
        Cube,
        Polygon,
        Voronoi,
        Patchy,
        PatchyLegacy,
        Patchy2D,
    };

  enum class Dimensionality
  {
    TwoDimensional,
    ThreeDimensional,
  };

    explicit TrajectoryReader(std::string path);

    bool isOpen() const;
    const std::string &error() const;
    size_t frameCount() const;
    FileType fileType() const;
  Dimensionality dimensionality() const;
    const std::vector<std::vector<bx::Vec3>> &voronoiPointSets() const;

    bool loadFrame(size_t frameIndex, ParticleSystem &particleSystem,
                   SimulationBox &simulationBox) const;

  private:
    bool scanFrames();

    std::string m_path;
    mutable std::string m_error;
    std::vector<std::streampos> m_frameOffsets;
    std::vector<std::vector<bx::Vec3>> m_voronoiPointSets;
    FileType m_fileType = FileType::Sphere;
    Dimensionality m_dimensionality = Dimensionality::ThreeDimensional;
};