#include "RodType.h"

#include <bx/math.h>

namespace
{

constexpr size_t kCylinderPartIndex = 0u;
constexpr size_t kPositiveCapPartIndex = 1u;
constexpr size_t kNegativeCapPartIndex = 2u;
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

    const bx::Vec3 zAxis = bx::mul(particle.direction, 1.0f / directionLength);
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

}

RodType::RodType(const bgfx::VertexLayout &layout, uint16_t stacks, uint16_t slices)
        : m_cylinderMesh(Mesh::createCylinder(1.0f, 1.0f, slices, layout)),
            m_capMesh(Mesh::createHemisphere(1.0f, stacks, slices, layout))
{
    m_parts.reserve(3u);

    m_parts.push_back(RenderPart{
        .mesh = &m_cylinderMesh,
        .localOffset = {0.0f, 0.0f, 0.0f},
        .localRotation = {0.0f, 0.0f, 0.0f},
        .baseScale = {1.0f, 1.0f, 1.0f},
        .scaleChannels = {0, 0, 1},
    });
    m_parts.push_back(RenderPart{
        .mesh = &m_capMesh,
        .localOffset = {0.0f, 0.0f, 0.0f},
        .localRotation = {0.0f, 0.0f, 0.0f},
        .baseScale = {1.0f, 1.0f, 1.0f},
        .scaleChannels = {0, 0, 2},
    });
    m_parts.push_back(RenderPart{
        .mesh = &m_capMesh,
        .localOffset = {0.0f, 0.0f, 0.0f},
        .localRotation = {bx::kPi, 0.0f, 0.0f},
        .baseScale = {1.0f, 1.0f, 1.0f},
        .scaleChannels = {0, 0, 2},
    });
}

const std::vector<RenderPart> &RodType::renderParts() const
{
    return m_parts;
}

void RodType::buildPartTransform(const Particle &particle, const float *parentTransform,
                                 size_t partIndex, float *outTransform) const
{
    const RenderPart &part = renderParts()[partIndex];
    const float radius = resolveScale(particle, 0u);
    const float cylinderLength = resolveScale(particle, 1u);
    const float capRadius = resolveScale(particle, 2u);

    float localOffsetZ = 0.0f;
    if (partIndex == kPositiveCapPartIndex)
    {
        localOffsetZ = 0.5f * cylinderLength;
    }
    else if (partIndex == kNegativeCapPartIndex)
    {
        localOffsetZ = -0.5f * cylinderLength;
    }

    float particleTransform[16];
    buildRigidTransformFromDirection(particle, particleTransform);

    float partTransform[16];
    const float scaleX = partIndex == kCylinderPartIndex ? radius : capRadius;
    const float scaleY = partIndex == kCylinderPartIndex ? radius : capRadius;
    const float scaleZ = partIndex == kCylinderPartIndex ? cylinderLength : capRadius;
    bx::mtxSRT(partTransform, scaleX, scaleY, scaleZ, part.localRotation.x,
               part.localRotation.y, part.localRotation.z, 0.0f, 0.0f, localOffsetZ);

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