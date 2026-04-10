#include "SphereType.h"

#include <bx/math.h>

// Fast path: a sphere's orientation doesn't affect its appearance, so we skip
// all trig-based rotation and directly build a uniform-scale + translate matrix.
void SphereType::buildPartTransform(const Particle &particle, const float *parentTransform,
                                    size_t /*partIndex*/, float *outTransform) const
{
    const float s = m_parts[0].baseScale.x * resolveScale(particle, m_parts[0].scaleChannels[0]);
    float modelTransform[16] = {};
    modelTransform[0]  = s;
    modelTransform[5]  = s;
    modelTransform[10] = s;
    modelTransform[12] = particle.position.x;
    modelTransform[13] = particle.position.y;
    modelTransform[14] = particle.position.z;
    modelTransform[15] = 1.0f;

    if (parentTransform != nullptr)
    {
        bx::mtxMul(outTransform, modelTransform, parentTransform);
    }
    else
    {
        bx::memCopy(outTransform, modelTransform, sizeof(modelTransform));
    }
}

SphereType::SphereType(const bgfx::VertexLayout &layout, float radius, uint16_t stacks,
                       uint16_t slices)
    : m_mesh(Mesh::createSphere(1.0f, stacks, slices, layout))
{
    m_parts.push_back(RenderPart{
        .mesh = &m_mesh,
        .localOffset = {0.0f, 0.0f, 0.0f},
        .localRotation = {0.0f, 0.0f, 0.0f},
        .baseScale = {radius, radius, radius},
        .scaleChannels = {0, 0, 0},
    });
}

const std::vector<RenderPart> &SphereType::renderParts() const
{
    return m_parts;
}