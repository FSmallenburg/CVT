#pragma once

#include "ParticleType.h"

/// ParticleType for cubic (.cub) particles. Renders each particle as an
/// axis-aligned cube; the cube half-extent is driven by sizeParams[0].
class CubeType final : public ParticleType
{
  public:
    explicit CubeType(const bgfx::VertexLayout &layout);

    const std::vector<RenderPart> &renderParts() const override;

  private:
    Mesh m_mesh;
    std::vector<RenderPart> m_parts;
};