#include "VoronoiType.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>

VoronoiType::VoronoiType(const bgfx::VertexLayout &layout,
                         const std::vector<VoronoiMeshData> &meshDataByShape)
{
    m_meshes.reserve(meshDataByShape.size());
    m_parts.reserve(meshDataByShape.size());

    for (const VoronoiMeshData &shapeMesh : meshDataByShape)
    {
        Mesh mesh;
        mesh.upload(shapeMesh.vertices, shapeMesh.indices, layout);
        m_meshes.push_back(std::move(mesh));
    }

    for (Mesh &mesh : m_meshes)
    {
        m_parts.push_back(RenderPart{
            .mesh = &mesh,
            .localOffset = {0.0f, 0.0f, 0.0f},
            .localRotation = {0.0f, 0.0f, 0.0f},
            .baseScale = {1.0f, 1.0f, 1.0f},
            .scaleChannels = {0, 0, 0},
        });
    }
}

const std::vector<RenderPart> &VoronoiType::renderParts() const
{
    return m_parts;
}

void VoronoiType::buildPartTransform(const Particle &particle, const float *parentTransform,
                                     size_t partIndex, float *outTransform) const
{
    if (m_parts.empty())
    {
        bx::mtxIdentity(outTransform);
        return;
    }

    const size_t selectedShapeIndex =
        static_cast<size_t>(std::max(0.0f, std::round(particle.sizeParams[3])))
        % m_parts.size();

    if (partIndex != selectedShapeIndex)
    {
        bx::mtxIdentity(outTransform);
        outTransform[0] = 0.0f;
        outTransform[5] = 0.0f;
        outTransform[10] = 0.0f;
        return;
    }

    ParticleType::buildPartTransform(particle, parentTransform, partIndex, outTransform);
}
