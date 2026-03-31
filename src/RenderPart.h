#pragma once

#include "Mesh.h"

#include <bx/math.h>

#include <array>
#include <cstdint>

struct RenderPart
{
    const Mesh *mesh = nullptr;
    bx::Vec3 localOffset{0.0f, 0.0f, 0.0f};
    bx::Vec3 localRotation{0.0f, 0.0f, 0.0f};
    bx::Vec3 baseScale{1.0f, 1.0f, 1.0f};
    std::array<uint8_t, 3> scaleChannels{0, 0, 0};
};