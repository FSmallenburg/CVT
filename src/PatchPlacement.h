#pragma once

#include <bx/math.h>

#include <cstddef>
#include <vector>

bool hasPatchPlacement(size_t patchCount);
const std::vector<bx::Vec3> &patchPlacementDirections(size_t patchCount);
const std::vector<bx::Vec3> &patchPlacementDirections(size_t patchCount, bool planar);