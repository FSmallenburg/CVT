#pragma once

#include "ParticleType.h"
#include "VoronoiCellBuilder.h"

#include <vector>

/// ParticleType for Voronoi (.voro) trajectory files. Each particle maps to a
/// unique cell mesh built by VoronoiCellBuilder from its Voronoi seed points.
/// The per-particle transform is a position translation only (no rotation or
/// scale from sizeParams).
class VoronoiType final : public ParticleType
{
  public:
    /// @param meshDataByShape One VoronoiMeshData entry per distinct Voronoi
    ///                        cell shape (indexed by particle ID order).
    VoronoiType(const bgfx::VertexLayout &layout,
                const std::vector<VoronoiMeshData> &meshDataByShape);

    const std::vector<RenderPart> &renderParts() const override;
    void buildPartTransform(const Particle &particle, const float *parentTransform,
                            size_t partIndex, float *outTransform) const override;

  private:
    std::vector<Mesh> m_meshes;
    std::vector<RenderPart> m_parts;
};
