#pragma once

#include <bx/math.h>

#include <array>
#include <cstdint>
#include <vector>

/// Holds all per-particle data that is read from a trajectory file and used
/// for rendering and analysis.
class Particle
{
  public:
    /// Single-character species label (e.g. 'A', 'B'). Determines palette slot.
    char typeLabel = 'A';
    bx::Vec3 position{0.0f, 0.0f, 0.0f};
    /// Euler-angle rotation (radians, ZXZ convention) — used by some file formats.
    bx::Vec3 rotation{0.0f, 0.0f, 0.0f};
    /// Unit direction vector, used by rods and disks (principal axis).
    bx::Vec3 direction{0.0f, 0.0f, 1.0f};
    /// Row-major 3×3 orientation matrix, used when hasOrientationMatrix is true.
    std::array<float, 9> orientationMatrix{1.0f, 0.0f, 0.0f,
                                           0.0f, 1.0f, 0.0f,
                                           0.0f, 0.0f, 1.0f};
    /// RGBA color read from the file. Preserved so color modes can restore it.
    std::array<float, 4> baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    /// RGBA color actually used for rendering (may be overridden by color modes).
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
    /// (rx, ry, rz, unused) radii/extents passed to the instanced shader.
    std::array<float, 4> sizeParams{1.0f, 1.0f, 1.0f, 1.0f};
    /// Additional per-particle order parameters loaded from `.osph` files.
    std::vector<float> orderParameters;
    bool visible = true;
    /// True when orientationMatrix has been populated (as opposed to direction).
    bool hasOrientationMatrix = false;
    /// Unique numeric ID assigned when the particle is added to the system.
    uint32_t id = 0;

    /// Sets all three size-parameter components to the same @p scale value.
    void setUniformScale(float scale)
    {
        sizeParams[0] = scale;
        sizeParams[1] = scale;
        sizeParams[2] = scale;
    }
};
