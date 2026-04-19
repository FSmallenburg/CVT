#pragma once

#include "ParticleSystem.h"
#include "SimulationBox.h"

#include <bx/math.h>

#include <cstddef>
#include <string>
#include <vector>

/// Reads multi-frame trajectory files produced by oxDNA-style simulation tools.
/// Supports a variety of file formats (sphere, rod, disk, patchy, voronoi,
/// …). After construction, check isOpen() / error() before use.
class TrajectoryReader
{
  public:
    /// The physical file format detected from the file extension and content.
    enum class FileType
    {
        Sphere,
        BondedSphere,
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

    /// Whether the simulation is two- or three-dimensional. Affects how
    /// coordinates and orientations are interpreted and rendered.
    enum class Dimensionality
    {
        TwoDimensional,
        ThreeDimensional,
    };

    /// Opens and scans @p path. fileType(), dimensionality(), and frameCount()
    /// are available immediately after construction (if isOpen() is true).
    explicit TrajectoryReader(std::string path);

    /// Returns true if the file was opened and scanned successfully.
    bool isOpen() const;

    /// Returns a human-readable description of the last error, or an empty
    /// string when the reader is healthy.
    const std::string &error() const;

    /// Returns the total number of frames discovered in the file.
    size_t frameCount() const;

    /// Returns the file format detected during construction.
    FileType fileType() const;

    /// Returns the dimensionality inferred during construction.
    Dimensionality dimensionality() const;

    /// Returns the per-frame Voronoi seed point sets (only non-empty for
    /// Voronoi files).
    const std::vector<std::vector<bx::Vec3>> &voronoiPointSets() const;

    /// Parses frame @p frameIndex and populates @p particleSystem and
    /// @p simulationBox. Returns false and sets error() on failure.
    bool loadFrame(size_t frameIndex, ParticleSystem &particleSystem,
                   SimulationBox &simulationBox) const;

  private:
    /// Scans the whole file to build the frame-offset table. Called once from
    /// the constructor.
    bool scanFrames();

    std::string m_path;
    mutable std::string m_error;
    std::vector<std::streampos> m_frameOffsets;
    std::vector<std::vector<bx::Vec3>> m_voronoiPointSets;
    FileType m_fileType = FileType::Sphere;
    Dimensionality m_dimensionality = Dimensionality::ThreeDimensional;
};