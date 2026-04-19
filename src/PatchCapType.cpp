#include "PatchCapType.h"
#include "BxVec3Operators.h"

#include <bx/math.h>

namespace
{

constexpr float kDirectionEpsilon = 1.0e-6f;

void buildRigidTransformFromDirection(const Particle &particle, float *outTransform)
{
    const float directionLength = bx::length(particle.direction);
    if (directionLength <= kDirectionEpsilon)
    {
        bx::mtxSRT(outTransform, 1.0f, 1.0f, 1.0f, particle.rotation.x, particle.rotation.y,
                   particle.rotation.z, particle.position.x, particle.position.y,
                   particle.position.z);
        return;
    }

    const bx::Vec3 zAxis = particle.direction * (1.0f / directionLength);
    const bx::Vec3 referenceAxis = std::abs(zAxis.z) < 0.999f
                                       ? bx::Vec3{0.0f, 0.0f, 1.0f}
                                       : bx::Vec3{0.0f, 1.0f, 0.0f};
    const bx::Vec3 xAxis = bx::normalize(bx::cross(referenceAxis, zAxis));
    const bx::Vec3 yAxis = bx::cross(zAxis, xAxis);

    outTransform[0] = xAxis.x;
    outTransform[1] = xAxis.y;
    outTransform[2] = xAxis.z;
    outTransform[3] = 0.0f;
    outTransform[4] = yAxis.x;
    outTransform[5] = yAxis.y;
    outTransform[6] = yAxis.z;
    outTransform[7] = 0.0f;
    outTransform[8] = zAxis.x;
    outTransform[9] = zAxis.y;
    outTransform[10] = zAxis.z;
    outTransform[11] = 0.0f;
    outTransform[12] = particle.position.x;
    outTransform[13] = particle.position.y;
    outTransform[14] = particle.position.z;
    outTransform[15] = 1.0f;
}

} // namespace

PatchCapType::PatchCapType(const bgfx::VertexLayout &layout, float cosHalfAngle,
                           uint16_t stacks, uint16_t slices)
    : m_capMesh(Mesh::createSphericalCap(1.0f, cosHalfAngle, stacks, slices, layout))
{
    m_parts.push_back(RenderPart{
        .mesh = &m_capMesh,
        .localOffset = {0.0f, 0.0f, 0.0f},
        .localRotation = {0.0f, 0.0f, 0.0f},
        .baseScale = {1.0f, 1.0f, 1.0f},
        .scaleChannels = {0, 0, 0},
    });
}

const std::vector<RenderPart> &PatchCapType::renderParts() const
{
    return m_parts;
}

void PatchCapType::buildPartTransform(const Particle &particle, const float *parentTransform,
                                      size_t, float *outTransform) const
{
    const float capRadius = resolveScale(particle, 0u);

    float particleTransform[16];
    buildRigidTransformFromDirection(particle, particleTransform);

    float partTransform[16];
    bx::mtxSRT(partTransform, capRadius, capRadius, capRadius, 0.0f, 0.0f, 0.0f,
               0.0f, 0.0f, 0.0f);

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