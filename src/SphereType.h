#pragma once

#include "ParticleType.h"

#include <cstdint>

/// ParticleType for sphere-file (.sph) particles. Renders each particle as a
/// UV sphere whose radius is taken from sizeParams[0].
class SphereType final : public ParticleType
{
  public:
    /// @param radius   Nominal sphere radius used when building the mesh.
    /// @param stacks   Number of latitude bands (more = smoother).
    /// @param slices   Number of longitude bands.
    SphereType(const bgfx::VertexLayout &layout, float radius, uint16_t stacks, uint16_t slices);

    const std::vector<RenderPart> &renderParts() const override;
    void buildPartTransform(const Particle &particle, const float *parentTransform,
                            size_t partIndex, float *outTransform) const override;

  private:
    Mesh m_mesh;
    std::vector<RenderPart> m_parts;
};