#pragma once

#include "ParticleType.h"

#include <cstdint>

class PolygonType final : public ParticleType
{
  public:
    PolygonType(const bgfx::VertexLayout &layout, uint16_t sideCount);

    const std::vector<RenderPart> &renderParts() const override;

  private:
    Mesh m_mesh;
    std::vector<RenderPart> m_parts;
};