#pragma once

#include "ParticleType.h"

/// ParticleType for octahedral particles. Renders each particle as a regular
/// octahedron; size is driven by sizeParams[0].
class OctahedronType final : public ParticleType
{
  public:
    explicit OctahedronType(const bgfx::VertexLayout &layout);

    const std::vector<RenderPart> &renderParts() const override;

  private:
    Mesh m_mesh;
    std::vector<RenderPart> m_parts;
};