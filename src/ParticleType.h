#pragma once

#include "Particle.h"
#include "RenderPart.h"

#include <cstddef>
#include <cstdint>
#include <vector>

/// Abstract base class for every particle geometry type (sphere, rod, cube,
/// patchy, …). Each concrete subclass owns the mesh(es) for its shape and
/// knows how to build the per-instance transform that is uploaded to the GPU.
///
/// The rendering pipeline queries renderParts() once per system to get the
/// list of sub-meshes, then calls buildPartTransform() for each part of each
/// visible particle to fill the instanced-draw data buffer.
class ParticleType
{
  public:
    virtual ~ParticleType() = default;

    /// Returns the list of RenderParts that make up this particle type.
    /// The returned vector must remain valid for the lifetime of the object.
    virtual const std::vector<RenderPart> &renderParts() const = 0;

    /// Builds the combined world-space 4×4 transform for @p particle and
    /// writes it into @p outTransform (column-major). The default
    /// implementation derives the transform from position, direction, and
    /// sizeParams[0].
    virtual void buildParticleTransform(const Particle &particle, float *outTransform) const;

    /// Builds the world-space transform for sub-part @p partIndex of @p particle
    /// starting from a pre-computed @p particleTransform and a @p parentTransform
    /// (e.g. the scene rotation). Writes the result into @p outTransform.
    virtual void buildPartTransformFromParticleTransform(const Particle &particle,
                                                         const float *particleTransform,
                                                         const float *parentTransform,
                                                         size_t partIndex,
                                                         float *outTransform) const;

    /// Builds the world-space transform for sub-part @p partIndex of @p particle
    /// directly from the raw particle data and @p parentTransform.
    /// This is the primary hot path called once per part per particle per frame.
    virtual void buildPartTransform(const Particle &particle, const float *parentTransform,
                                    size_t partIndex, float *outTransform) const;

  protected:
    /// Returns the scale value for @p channel (0–3) from @p particle.sizeParams,
    /// falling back to sizeParams[0] if @p channel is out of range.
    static float resolveScale(const Particle &particle, uint8_t channel);
};