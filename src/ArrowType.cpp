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

/// Builds the transform of the shaft (part 0) or tip (part 1) of an arrow that
/// starts at particle.position and points along particle.direction.
void buildArrowPartTransform(const Particle &particle, float shaftRadius, float shaftLength,
                             float tipRadius, float tipLength, const float *parentTransform,
                             size_t partIndex, float *outTransform)
{
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
    buildArrowPartTransform(particle, resolveScale(particle, 0u), resolveScale(particle, 1u),
                            resolveScale(particle, 2u), resolveScale(particle, 3u),
                            parentTransform, partIndex, outTransform);
}

PropulsionArrowType::PropulsionArrowType(const bgfx::VertexLayout &layout, uint16_t slices)
    : m_arrow(layout, slices)
{
}

const std::vector<RenderPart> &PropulsionArrowType::renderParts() const
{
    return m_arrow.renderParts();
}

void PropulsionArrowType::buildPartTransform(const Particle &particle,
                                             const float *parentTransform, size_t partIndex,
                                             float *outTransform) const
{
    const float radius = particle.sizeParams[0];
    const float length = particle.sizeParams[1];
    const float directionLength = bx::length(particle.direction);

    const float shaftRadius = kShaftRadiusFraction * radius;
    const float desiredTipLength = kTipLengthFraction * radius;
    // Keep at least half of a short arrow as shaft, so its direction stays
    // readable instead of collapsing into a lone triangle.
    const float tipLength = bx::min(desiredTipLength, 0.5f * length);
    const float tipScale = desiredTipLength > 1.0e-6f ? tipLength / desiredTipLength : 1.0f;
    const float tipRadius = kTipRadiusFraction * radius * tipScale;
    const float shaftLength = bx::max(0.0f, length - tipLength);

    // Start half the arrow length behind the particle center, and lift the
    // arrow so even its widest part (the tip) clears the particle's top.
    Particle shifted = particle;
    if (directionLength > 1.0e-6f)
    {
        const bx::Vec3 axis = particle.direction * (1.0f / directionLength);
        shifted.position = particle.position - axis * (0.5f * length);
    }
    shifted.position.z += kLiftFraction * radius + kTipRadiusFraction * radius;

    buildArrowPartTransform(shifted, shaftRadius, shaftLength, tipRadius, tipLength,
                            parentTransform, partIndex, outTransform);
}
