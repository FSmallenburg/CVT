#include "PolygonType.h"

PolygonType::PolygonType(const bgfx::VertexLayout &layout, uint16_t sideCount)
    : m_mesh(Mesh::createRegularPolygon(sideCount, layout))
{
    m_parts.push_back(RenderPart{
        .mesh = &m_mesh,
        .localOffset = {0.0f, 0.0f, 0.0f},
        .localRotation = {0.0f, 0.0f, 0.0f},
        .baseScale = {1.0f, 1.0f, 1.0f},
        .scaleChannels = {0, 0, 3},
    });
}

const std::vector<RenderPart> &PolygonType::renderParts() const
{
    return m_parts;
}