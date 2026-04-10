#include "ParticleType.h"

#include <bx/math.h>

void ParticleType::buildParticleTransform(const Particle &particle, float *outTransform) const
{
    if (particle.hasOrientationMatrix)
    {
        outTransform[0] = particle.orientationMatrix[0];
        outTransform[1] = particle.orientationMatrix[3];
        outTransform[2] = particle.orientationMatrix[6];
        outTransform[3] = 0.0f;
        outTransform[4] = particle.orientationMatrix[1];
        outTransform[5] = particle.orientationMatrix[4];
        outTransform[6] = particle.orientationMatrix[7];
        outTransform[7] = 0.0f;
        outTransform[8] = particle.orientationMatrix[2];
        outTransform[9] = particle.orientationMatrix[5];
        outTransform[10] = particle.orientationMatrix[8];
        outTransform[11] = 0.0f;
        outTransform[12] = particle.position.x;
        outTransform[13] = particle.position.y;
        outTransform[14] = particle.position.z;
        outTransform[15] = 1.0f;
        return;
    }

    bx::mtxSRT(outTransform, 1.0f, 1.0f, 1.0f, particle.rotation.x, particle.rotation.y,
               particle.rotation.z, particle.position.x, particle.position.y, particle.position.z);
}

float ParticleType::resolveScale(const Particle &particle, uint8_t channel)
{
    return channel < particle.sizeParams.size() ? particle.sizeParams[channel] : 1.0f;
}

void ParticleType::buildPartTransformFromParticleTransform(const Particle &particle,
                                                           const float *particleTransform,
                                                           const float *parentTransform,
                                                           size_t partIndex,
                                                           float *outTransform) const
{
    const RenderPart &part = renderParts()[partIndex];

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

void ParticleType::buildPartTransform(const Particle &particle, const float *parentTransform,
                                      size_t partIndex, float *outTransform) const
{
    float particleTransform[16];
    buildParticleTransform(particle, particleTransform);
    buildPartTransformFromParticleTransform(particle, particleTransform, parentTransform, partIndex,
                                            outTransform);
}