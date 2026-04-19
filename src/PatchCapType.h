#pragma once

#include "ParticleType.h"

/// ParticleType for the spherical-cap "patch" on a patchy particle. One
/// PatchCapType instance is created per distinct half-angle used in the file.
/// The cap is positioned along the patch direction vector; its size is driven
/// by the parent patchy particle's coreRadius and capRadius.
class PatchCapType final : public ParticleType
{
  public:
    /// @param cosHalfAngle cos(half-opening-angle) of the cap.
    /// @param stacks       Latitude bands for the spherical cap mesh.
    /// @param slices       Longitude bands.
    PatchCapType(const bgfx::VertexLayout &layout, float cosHalfAngle, uint16_t stacks,
                 uint16_t slices);

    const std::vector<RenderPart> &renderParts() const override;
    void buildPartTransform(const Particle &particle, const float *parentTransform,
                            size_t partIndex, float *outTransform) const override;

  private:
    Mesh m_capMesh;
    std::vector<RenderPart> m_parts;
};