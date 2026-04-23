#include "ColorPalette.h"

#include <array>
#include <cctype>

namespace
{

constexpr std::array<std::array<float, 4>, kParticlePaletteColorCount> kParticlePalette = {{
    {0.6f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.6f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.6f, 1.0f},
    {0.6f, 0.6f, 0.0f, 1.0f},
    {1.0f, 0.6f, 0.6f, 1.0f},
    {0.6f, 0.0f, 0.6f, 1.0f},
    {0.4f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.4f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.4f, 1.0f},
    {0.4f, 0.4f, 0.0f, 1.0f},
    {0.0f, 0.4f, 0.4f, 1.0f},
    {0.4f, 0.0f, 0.4f, 1.0f},
    {0.3f, 0.6f, 0.0f, 1.0f},
    {0.6f, 0.3f, 0.0f, 1.0f},
    {0.0f, 0.3f, 0.6f, 1.0f},
    {0.0f, 0.6f, 0.3f, 1.0f},
    {0.6f, 0.0f, 0.3f, 1.0f},
    {0.3f, 0.0f, 0.6f, 1.0f},
    {0.6f, 0.6f, 0.6f, 1.0f},
    {0.3f, 0.6f, 0.6f, 1.0f},
    {0.6f, 0.3f, 0.6f, 1.0f},
    {0.6f, 0.6f, 0.3f, 1.0f},
    {0.6f, 0.3f, 0.3f, 1.0f},
    {0.3f, 0.3f, 0.6f, 1.0f},
    {0.5f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.6f, 0.6f, 1.0f},
}};

} // namespace

std::array<float, 4> colorFromLetter(char letter)
{
    const int index = std::toupper(static_cast<unsigned char>(letter)) - 'A';
    return colorFromPaletteIndex(static_cast<size_t>((index + kParticlePaletteColorCount)
                                                     % kParticlePaletteColorCount));
}

std::array<float, 4> colorFromPaletteIndex(size_t index)
{
    return kParticlePalette[index % kParticlePaletteColorCount];
}

std::array<float, 4> highlightColor(const std::array<float, 4> &color)
{
    return {1.0f - color[0], 1.0f - color[1], 1.0f - color[2], color[3]};
}