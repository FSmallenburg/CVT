#pragma once

#include "ParticleType.h"

/// ParticleType for the cone that visualises a patch interaction zone on a
/// patchy particle. The cone apex sits at the particle center and opens in the
/// patch direction; the half-angle is baked into the mesh at construction.
class PatchConeType final : public ParticleType
{
  public:
    /// @param slices Number of faces around the cone base circumference.
    PatchConeType(const bgfx::VertexLayout &layout, uint16_t slices);

    const std::vector<RenderPart> &renderParts() const override;
    void buildPartTransform(const Particle &particle, const float *parentTransform,
                            size_t partIndex, float *outTransform) const override;

  private:
    Mesh m_coneMesh;
    std::vector<RenderPart> m_parts;
};