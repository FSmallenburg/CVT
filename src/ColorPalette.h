#pragma once

#include <array>
#include <cstddef>

constexpr size_t kParticlePaletteColorCount = 26;

std::array<float, 4> colorFromLetter(char letter);
std::array<float, 4> colorFromPaletteIndex(size_t index);