#include "PatchPlacement.h"

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