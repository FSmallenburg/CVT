#pragma once

#include "Particle.h"
#include "RenderPart.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class ParticleType
{
  public:
    virtual ~ParticleType() = default;

    virtual const std::vector<RenderPart> &renderParts() const = 0;

    virtual void buildParticleTransform(const Particle &particle, float *outTransform) const;

    virtual void buildPartTransformFromParticleTransform(const Particle &particle,
                                                         const float *particleTransform,
                                                         const float *parentTransform,
                                                         size_t partIndex,
                                                         float *outTransform) const;

    virtual void buildPartTransform(const Particle &particle, const float *parentTransform,
                                    size_t partIndex, float *outTransform) const;

  protected:
    static float resolveScale(const Particle &particle, uint8_t channel);
};