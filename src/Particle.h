#pragma once

#include <bx/math.h>

#include <array>
#include <cstdint>

class Particle
{
  public:
    bx::Vec3 position{0.0f, 0.0f, 0.0f};
    bx::Vec3 rotation{0.0f, 0.0f, 0.0f};
    bx::Vec3 direction{0.0f, 0.0f, 1.0f};
    std::array<float, 9> orientationMatrix{1.0f, 0.0f, 0.0f,
                                           0.0f, 1.0f, 0.0f,
                                           0.0f, 0.0f, 1.0f};
    std::array<float, 4> baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> sizeParams{1.0f, 1.0f, 1.0f, 1.0f};
    bool visible = true;
    bool hasOrientationMatrix = false;
    uint32_t id = 0;

    void setUniformScale(float scale)
    {
        sizeParams[0] = scale;
        sizeParams[1] = scale;
        sizeParams[2] = scale;
    }
};
