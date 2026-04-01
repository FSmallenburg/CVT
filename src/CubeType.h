#pragma once

#include "ParticleType.h"

class CubeType final : public ParticleType
{
  public:
    explicit CubeType(const bgfx::VertexLayout &layout);

    const std::vector<RenderPart> &renderParts() const override;

  private:
    Mesh m_mesh;
    std::vector<RenderPart> m_parts;
};