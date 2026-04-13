#pragma once

#include "Mesh.h"

#include <bx/math.h>

#include <string>
#include <vector>

struct VoronoiMeshData
{
    std::vector<PosNormalVertex> vertices;
    std::vector<uint16_t> indices;
};

bool buildVoronoiCellMesh(const std::vector<bx::Vec3> &sites,
                          VoronoiMeshData &outMesh,
                          std::string &error);
