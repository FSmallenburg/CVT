#pragma once

#include <bx/math.h>

#include <cstddef>
#include <vector>

/// Returns true when a pre-computed patch-direction table exists for
/// @p patchCount patches (currently supported up to a fixed maximum).
bool hasPatchPlacement(size_t patchCount);

/// Returns the pre-computed unit direction vectors for @p patchCount 3-D
/// patches, distributed uniformly on the sphere surface.
const std::vector<bx::Vec3> &patchPlacementDirections(size_t patchCount);

/// Returns directions for @p patchCount patches. When @p planar is true,
/// returns the 2-D in-plane variant (all z = 0).
const std::vector<bx::Vec3> &patchPlacementDirections(size_t patchCount, bool planar);