#pragma once

#include "ParticleType.h"

class RodType final : public ParticleType
{
  public:
    RodType(const bgfx::VertexLayout &layout, uint16_t stacks, uint16_t slices);

    const std::vector<RenderPart> &renderParts() const override;
    void buildPartTransform(const Particle &particle, const float *parentTransform,
                            size_t partIndex, float *outTransform) const override;

  private:
    Mesh m_cylinderMesh;
    Mesh m_capMesh;
    std::vector<RenderPart> m_parts;
};