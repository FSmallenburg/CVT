#include "OctahedronType.h"

OctahedronType::OctahedronType(const bgfx::VertexLayout &layout)
    : m_mesh(Mesh::createOctahedron(0.5f, layout))
{
    m_parts.push_back(RenderPart{
        .mesh = &m_mesh,
        .localOffset = {0.0f, 0.0f, 0.0f},
        .localRotation = {0.0f, 0.0f, 0.0f},
        .baseScale = {1.0f, 1.0f, 1.0f},
        .scaleChannels = {0, 0, 0},
    });
}

const std::vector<RenderPart> &OctahedronType::renderParts() const
{
    return m_parts;
}