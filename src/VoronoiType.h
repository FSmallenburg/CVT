#pragma once

#include "ParticleType.h"
#include "VoronoiCellBuilder.h"

#include <vector>

class VoronoiType final : public ParticleType
{
  public:
    VoronoiType(const bgfx::VertexLayout &layout,
                const std::vector<VoronoiMeshData> &meshDataByShape);

    const std::vector<RenderPart> &renderParts() const override;
    void buildPartTransform(const Particle &particle, const float *parentTransform,
                            size_t partIndex, float *outTransform) const override;

  private:
    std::vector<Mesh> m_meshes;
    std::vector<RenderPart> m_parts;
};
