#include "ArrowType.h"
#include "BxVec3Operators.h"

#include <bx/math.h>

namespace
{

constexpr size_t kShaftPartIndex = 0u;
constexpr size_t kHeadPartIndex = 1u;
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

ArrowType::ArrowType(const bgfx::VertexLayout &layout, uint16_t slices)
        : m_shaftMesh(Mesh::createCylinder(1.0f, 1.0f, slices, layout)),
            m_headMesh(Mesh::createCone(1.0f, 1.0f, slices, layout))
{
    m_parts.push_back(RenderPart{
        .mesh = &m_shaftMesh,
        .localOffset = {0.0f, 0.0f, 0.0f},
        .localRotation = {0.0f, 0.0f, 0.0f},
        .baseScale = {1.0f, 1.0f, 1.0f},
        .scaleChannels = {0, 0, 1},
    });
    m_parts.push_back(RenderPart{
        .mesh = &m_headMesh,
        .localOffset = {0.0f, 0.0f, 0.0f},
        .localRotation = {0.0f, 0.0f, 0.0f},
        .baseScale = {1.0f, 1.0f, 1.0f},
        .scaleChannels = {2, 2, 3},
    });
}

const std::vector<RenderPart> &ArrowType::renderParts() const
{
    return m_parts;
}

void ArrowType::buildPartTransform(const Particle &particle, const float *parentTransform,
                                   size_t partIndex, float *outTransform) const
{
    const float shaftRadius = resolveScale(particle, 0u);
    const float shaftLength = resolveScale(particle, 1u);
    const float tipRadius = resolveScale(particle, 2u);
    const float tipLength = resolveScale(particle, 3u);

    float particleTransform[16];
    buildRigidTransformFromDirection(particle, particleTransform);

    float partTransform[16];
    if (partIndex == kShaftPartIndex)
    {
        bx::mtxSRT(partTransform, shaftRadius, shaftRadius, shaftLength, 0.0f, 0.0f, 0.0f,
                   0.0f, 0.0f, 0.5f * shaftLength);
    }
    else
    {
        bx::mtxSRT(partTransform, tipRadius, tipRadius, tipLength, 0.0f, 0.0f, 0.0f,
                   0.0f, 0.0f, shaftLength);
    }

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