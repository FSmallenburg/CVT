#pragma once

#include "Mesh.h"

#include <bx/math.h>

#include <array>
#include <cstdint>

/// Describes one geometric sub-part of a particle type (e.g. the shaft of a
/// rod, or a patch cap). A ParticleType holds one or more RenderParts; each
/// part references a Mesh and carries local transform data that is combined
/// with the per-particle transform at render time.
struct RenderPart
{
    /// Pointer to the shared Mesh used by this part (not owned).
    const Mesh *mesh = nullptr;
    /// Translation applied in the part's local space before the particle transform.
    bx::Vec3 localOffset{0.0f, 0.0f, 0.0f};
    /// Euler-angle rotation (radians) applied in the part's local space.
    bx::Vec3 localRotation{0.0f, 0.0f, 0.0f};
    /// Base scale factors (x, y, z) before per-particle size parameters are applied.
    bx::Vec3 baseScale{1.0f, 1.0f, 1.0f};
    /// Which of the particle's sizeParams channels (0–3) drives each scale axis.
    std::array<uint8_t, 3> scaleChannels{0, 0, 0};
};