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

/// Arrow drawn on top of a 2-D particle to show its propulsion vector. Unlike
/// ArrowType it is centered on particle.position and lifted just above the
/// particle's surface along +z, and all proportions follow from two size
/// parameters, so the arrow scales with the particle-size slider:
///   sizeParams[0] = particle radius R, sizeParams[1] = total arrow length L.
/// The arrow points along particle.direction.
class PropulsionArrowType final : public ParticleType
{
  public:
    static constexpr float kShaftRadiusFraction = 0.10f; ///< shaft radius / R
    static constexpr float kTipRadiusFraction = 0.22f;   ///< full tip radius / R
    static constexpr float kTipLengthFraction = 0.60f;   ///< full tip length / R; short
                                                         ///< arrows use at most half their
                                                         ///< length for the (shrunk) tip
    static constexpr float kLiftFraction = 1.01f;        ///< axis height above center / R,
                                                         ///< plus one tip radius

    PropulsionArrowType(const bgfx::VertexLayout &layout, uint16_t slices);

    const std::vector<RenderPart> &renderParts() const override;
    void buildPartTransform(const Particle &particle, const float *parentTransform,
                            size_t partIndex, float *outTransform) const override;

  private:
    ArrowType m_arrow; ///< Supplies the shaft and tip meshes.
};