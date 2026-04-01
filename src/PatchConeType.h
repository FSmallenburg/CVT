#pragma once

#include "ParticleType.h"

class PatchConeType final : public ParticleType
{
  public:
    PatchConeType(const bgfx::VertexLayout &layout, uint16_t slices);

    const std::vector<RenderPart> &renderParts() const override;
    void buildPartTransform(const Particle &particle, const float *parentTransform,
                            size_t partIndex, float *outTransform) const override;

  private:
    Mesh m_coneMesh;
    std::vector<RenderPart> m_parts;
};