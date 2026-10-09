#pragma once

#include "ParticleType.h"

#include <cstdint>

/// ParticleType for polygon (.gon) particles. Renders each particle as a flat
/// regular polygon lying in the XY plane. The number of sides is fixed at
/// construction; the size (circumradius) is driven by sizeParams[0].
class PolygonType final : public ParticleType
{
  public:
    /// Number of segments used to draw a disk (side count 0).
    static constexpr uint16_t kDiskSegmentCount = 64u;

    /// @param sideCount Number of sides of the regular polygon (e.g. 6 = hexagon),
    ///                  or 0 for a disk.
    PolygonType(const bgfx::VertexLayout &layout, uint16_t sideCount);

    const std::vector<RenderPart> &renderParts() const override;

  private:
    Mesh m_mesh;
    std::vector<RenderPart> m_parts;
};