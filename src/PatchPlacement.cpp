#include "PatchPlacement.h"

#include <bx/constants.h>

#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace
{

const std::unordered_map<size_t, std::vector<bx::Vec3>> kPatchPlacements = {
    {1u, {{0.0f, 0.0f, 1.0f}}},
    {2u, {{0.0f, 0.0f, 1.0f},
          {0.0f, 0.0f, -1.0f}}},
    {3u, {{0.0f, 0.0f, 1.0f},
          {0.8660254f, 0.0f, -0.5f},
          {-0.8660254f, 0.0f, -0.5f}}},
    {4u, {{0.57735027f, 0.57735027f, 0.57735027f},
          {-0.57735027f, -0.57735027f, 0.57735027f},
          {0.57735027f, -0.57735027f, -0.57735027f},
          {-0.57735027f, 0.57735027f, -0.57735027f}}},
    {5u, {{0.0f, 0.0f, 1.0f},
          {0.0f, 0.0f, -1.0f},
          {1.0f, 0.0f, 0.0f},
          {-0.5f, 0.8660254f, 0.0f},
          {-0.5f, -0.8660254f, 0.0f}}},
    {6u, {{1.0f, 0.0f, 0.0f},
          {0.0f, 1.0f, 0.0f},
          {0.0f, 0.0f, 1.0f},
          {-1.0f, 0.0f, 0.0f},
          {0.0f, -1.0f, 0.0f},
          {0.0f, 0.0f, -1.0f}}},
    {8u, {{-0.607781f, -0.607781f, -0.511081f},
          {-0.607781f, 0.607781f, -0.511081f},
          {0.0f, -0.859533f, 0.511081f},
          {0.0f, 0.859533f, 0.511081f},
          {0.607781f, -0.607781f, -0.511081f},
          {0.607781f, 0.607781f, -0.511081f},
          {-0.859533f, 0.0f, 0.511081f},
          {0.859533f, 0.0f, 0.511081f}}},
    {12u, {{-0.5257311f, 0.0f, -0.8506508f},
           {-0.5257311f, 0.0f, 0.8506508f},
           {0.5257311f, 0.0f, -0.8506508f},
           {0.5257311f, 0.0f, 0.8506508f},
           {0.0f, -0.8506508f, -0.5257311f},
           {0.0f, -0.8506508f, 0.5257311f},
           {0.0f, 0.8506508f, -0.5257311f},
           {0.0f, 0.8506508f, 0.5257311f},
           {-0.8506508f, -0.5257311f, 0.0f},
           {-0.8506508f, 0.5257311f, 0.0f},
           {0.8506508f, -0.5257311f, 0.0f},
           {0.8506508f, 0.5257311f, 0.0f}}},
};

std::unordered_map<size_t, std::vector<bx::Vec3>> kPlanarPatchPlacements;

} // namespace

bool hasPatchPlacement(size_t patchCount)
{
    return kPatchPlacements.contains(patchCount);
}

const std::vector<bx::Vec3> &patchPlacementDirections(size_t patchCount)
{
    const auto placementIt = kPatchPlacements.find(patchCount);
    if (placementIt == kPatchPlacements.end())
    {
        throw std::out_of_range("Unsupported patch count");
    }
    return placementIt->second;
}

const std::vector<bx::Vec3> &patchPlacementDirections(size_t patchCount, bool planar)
{
      if (!planar)
      {
            return patchPlacementDirections(patchCount);
      }

      auto placementIt = kPlanarPatchPlacements.find(patchCount);
      if (placementIt != kPlanarPatchPlacements.end())
      {
            return placementIt->second;
      }

      if (patchCount == 0u)
      {
            throw std::out_of_range("Unsupported patch count");
      }

      std::vector<bx::Vec3> directions;
      directions.reserve(patchCount);
      for (size_t patchIndex = 0; patchIndex < patchCount; ++patchIndex)
      {
            const float phi = 2.0f * bx::kPi * static_cast<float>(patchIndex)
                                      / static_cast<float>(patchCount);
            directions.push_back({std::cos(phi), std::sin(phi), 0.0f});
      }

      placementIt = kPlanarPatchPlacements.emplace(patchCount, std::move(directions)).first;
      return placementIt->second;
}