#pragma once

#include "ParticleType.h"

class CylinderType final : public ParticleType
{
  public:
    CylinderType(const bgfx::VertexLayout &layout, uint16_t slices);

    const std::vector<RenderPart> &renderParts() const override;
    void buildPartTransform(const Particle &particle, const float *parentTransform,
                            size_t partIndex, float *outTransform) const override;

  private:
    Mesh m_cylinderMesh;
    std::vector<RenderPart> m_parts;
};