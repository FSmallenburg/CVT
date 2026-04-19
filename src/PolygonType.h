#pragma once

#include "ParticleType.h"

#include <cstdint>

/// ParticleType for polygon (.ptc) disk-like particles. Renders each particle
/// as a flat regular polygon lying in the XY plane. The number of sides is
/// fixed at construction; the size is driven by sizeParams[0].
class PolygonType final : public ParticleType
{
  public:
    /// @param sideCount Number of sides of the regular polygon (e.g. 6 = hexagon).
    PolygonType(const bgfx::VertexLayout &layout, uint16_t sideCount);

    const std::vector<RenderPart> &renderParts() const override;

  private:
    Mesh m_mesh;
    std::vector<RenderPart> m_parts;
};