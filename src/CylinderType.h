#pragma once

#include "ParticleType.h"

/// ParticleType that renders a single open cylinder, typically used for
/// bond line segments connecting two particles. sizeParams[0] controls the
/// cylinder radius and sizeParams[2] controls the length.
class CylinderType final : public ParticleType
{
  public:
    /// @param slices Number of faces around the cylinder circumference.
    CylinderType(const bgfx::VertexLayout &layout, uint16_t slices);

    const std::vector<RenderPart> &renderParts() const override;
    void buildPartTransform(const Particle &particle, const float *parentTransform,
                            size_t partIndex, float *outTransform) const override;

  private:
    Mesh m_cylinderMesh;
    std::vector<RenderPart> m_parts;
};