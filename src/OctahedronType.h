#pragma once

#include "ParticleType.h"

class OctahedronType final : public ParticleType
{
  public:
    explicit OctahedronType(const bgfx::VertexLayout &layout);

    const std::vector<RenderPart> &renderParts() const override;

  private:
    Mesh m_mesh;
    std::vector<RenderPart> m_parts;
};