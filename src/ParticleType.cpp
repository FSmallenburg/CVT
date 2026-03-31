#include "ParticleType.h"

#include <bx/math.h>

float ParticleType::resolveScale(const Particle &particle, uint8_t channel)
{
    return channel < particle.sizeParams.size() ? particle.sizeParams[channel] : 1.0f;
}

void ParticleType::buildPartTransform(const Particle &particle, const float *parentTransform,
                                      size_t partIndex, float *outTransform) const
{
    const RenderPart &part = renderParts()[partIndex];

    float particleTransform[16];
    bx::mtxSRT(particleTransform, 1.0f, 1.0f, 1.0f, particle.rotation.x, particle.rotation.y,
               particle.rotation.z, particle.position.x, particle.position.y, particle.position.z);

    float partTransform[16];
    bx::mtxSRT(partTransform, part.baseScale.x * resolveScale(particle, part.scaleChannels[0]),
               part.baseScale.y * resolveScale(particle, part.scaleChannels[1]),
               part.baseScale.z * resolveScale(particle, part.scaleChannels[2]),
               part.localRotation.x, part.localRotation.y, part.localRotation.z, part.localOffset.x,
               part.localOffset.y, part.localOffset.z);

    float modelTransform[16];
    bx::mtxMul(modelTransform, partTransform, particleTransform);

    if (parentTransform != nullptr)
    {
        bx::mtxMul(outTransform, modelTransform, parentTransform);
    }
    else
    {
        bx::memCopy(outTransform, modelTransform, sizeof(modelTransform));
    }
}