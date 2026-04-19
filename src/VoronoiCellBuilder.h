#pragma once

#include "Mesh.h"

#include <bx/math.h>

#include <string>
#include <vector>

/// CPU-side vertex and index data for a single Voronoi cell mesh, before it
/// is uploaded to the GPU as a Mesh.
struct VoronoiMeshData
{
    std::vector<PosNormalVertex> vertices;
    std::vector<uint16_t> indices;
};

/// Computes the Voronoi cell of the first point in @p sites (i.e. the region
/// closer to sites[0] than to any other site) and writes the triangulated
/// mesh into @p outMesh. Returns false and sets @p error on failure.
bool buildVoronoiCellMesh(const std::vector<bx::Vec3> &sites,
                          VoronoiMeshData &outMesh,
                          std::string &error);
