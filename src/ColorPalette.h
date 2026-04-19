#pragma once

#include <array>
#include <cstddef>

/// Number of distinct particle-type colors in the palette (one per letter A–Z).
constexpr size_t kParticlePaletteColorCount = 26;

/// Returns the RGBA color assigned to the particle-type identified by @p letter
/// (case-insensitive, e.g. 'A' or 'a'). Falls back to white for unknown letters.
std::array<float, 4> colorFromLetter(char letter);

/// Returns the RGBA color at zero-based palette @p index
/// (0 = 'A', 1 = 'B', …, 25 = 'Z').
std::array<float, 4> colorFromPaletteIndex(size_t index);