#include "SphereType.h"

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