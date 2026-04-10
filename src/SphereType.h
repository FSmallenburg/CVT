#pragma once

#include "ParticleType.h"

#include <cstdint>

class SphereType final : public ParticleType
{
  public:
    SphereType(const bgfx::VertexLayout &layout, float radius, uint16_t stacks, uint16_t slices);

    const std::vector<RenderPart> &renderParts() const override;
    void buildPartTransform(const Particle &particle, const float *parentTransform,
                            size_t partIndex, float *outTransform) const override;

  private:
    Mesh m_mesh;
    std::vector<RenderPart> m_parts;
};