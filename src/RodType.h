#pragma once

#include "ParticleType.h"

/// ParticleType for rod (.rod) particles. Renders each particle as a
/// capped cylinder (open cylinder + two hemispherical end-caps). The
/// cylinder length is determined by sizeParams[2]; the radius by sizeParams[0].
class RodType final : public ParticleType
{
  public:
    /// @param stacks   Latitude bands for the hemispherical caps.
    /// @param slices   Longitude bands for both the cylinder and caps.
    RodType(const bgfx::VertexLayout &layout, uint16_t stacks, uint16_t slices);

    const std::vector<RenderPart> &renderParts() const override;
    void buildPartTransform(const Particle &particle, const float *parentTransform,
                            size_t partIndex, float *outTransform) const override;

  private:
    Mesh m_cylinderMesh;
    Mesh m_capMesh;
    std::vector<RenderPart> m_parts;
};