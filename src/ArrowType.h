#pragma once

#include "ParticleType.h"

/// ParticleType that renders a cylinder shaft plus a cone arrowhead, used to
/// visualise particle orientations and mobility vectors. The shaft is oriented
/// along the particle's direction vector; sizeParams controls shaft radius and
/// length.
class ArrowType final : public ParticleType
{
  public:
    /// @param slices Number of faces around the shaft/cone circumference.
    ArrowType(const bgfx::VertexLayout &layout, uint16_t slices);

    const std::vector<RenderPart> &renderParts() const override;
    void buildPartTransform(const Particle &particle, const float *parentTransform,
                            size_t partIndex, float *outTransform) const override;

  private:
    Mesh m_shaftMesh;
    Mesh m_headMesh;
    std::vector<RenderPart> m_parts;
};