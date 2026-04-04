#include "StructureFactor.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace
{

constexpr double kPi = 3.14159265358979323846;

struct ReciprocalIndex
{
    int a = 0;
    int b = 0;
    int c = 0;

    bool operator==(const ReciprocalIndex &other) const = default;
};

struct ReciprocalIndexHash
{
    size_t operator()(const ReciprocalIndex &index) const noexcept
    {
        size_t seed = std::hash<int>{}(index.a);
        seed ^= std::hash<int>{}(index.b) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        seed ^= std::hash<int>{}(index.c) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        return seed;
    }
};

bool isCentralPeak(const ReciprocalIndex &index)
{
    return index.a == 0 && index.b == 0 && index.c == 0;
}

bx::Vec3 rotateScreenWaveVectorToBoxFrame(const std::array<float, 16> &rotation,
                                          const bx::Vec3 &screenVector)
{
    return {
        rotation[0] * screenVector.x + rotation[1] * screenVector.y
            + rotation[2] * screenVector.z,
        rotation[4] * screenVector.x + rotation[5] * screenVector.y
            + rotation[6] * screenVector.z,
        rotation[8] * screenVector.x + rotation[9] * screenVector.y
            + rotation[10] * screenVector.z,
    };
}

bool collectStructureFactorParticles(const ParticleSystem &particleSystem,
                                     bool useVisibleParticlesOnly,
                                     std::vector<const Particle *> &sampledParticles,
                                     std::string &error)
{
    sampledParticles.clear();
    sampledParticles.reserve(particleSystem.particles().size());
    for (const Particle &particle : particleSystem.particles())
    {
        if (!useVisibleParticlesOnly || particle.visible)
        {
            sampledParticles.push_back(&particle);
        }
    }

    if (sampledParticles.empty())
    {
        error = useVisibleParticlesOnly
                    ? "No visible particles are available for the structure factor image."
                    : "No particles are available for the structure factor image.";
        return false;
    }

    return true;
}

uint16_t chooseParticleTextureSide(size_t particleCount)
{
    const double sideLength = std::ceil(std::sqrt(double(std::max<size_t>(particleCount, 1u))));
    return static_cast<uint16_t>(std::clamp<uint32_t>(static_cast<uint32_t>(sideLength),
                                                      1u, 4096u));
}

float structureFactorValueForIndex(const std::vector<const Particle *> &sampledParticles,
                                   const ReciprocalIndex &index,
                                   double stepX, double stepY, double stepZ,
                                   bool logScale)
{
    const double kx = stepX * double(index.a);
    const double ky = stepY * double(index.b);
    const double kz = stepZ * double(index.c);

    double sumReal = 0.0;
    double sumImag = 0.0;
    for (const Particle *particle : sampledParticles)
    {
        const bx::Vec3 &position = particle->position;
        const double phase = kx * double(position.x) + ky * double(position.y)
                             + kz * double(position.z);
        sumReal += std::cos(phase);
        sumImag += std::sin(phase);
    }

    const double inverseParticleCount = 1.0 / double(sampledParticles.size());
    const double intensity = inverseParticleCount * (sumReal * sumReal + sumImag * sumImag);
    return logScale ? float(std::log1p(intensity)) : float(intensity);
}

std::array<float, 4> structureFactorColor(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);

    constexpr std::array<std::array<float, 3>, 5> kStops = {{
        {{0.02f, 0.02f, 0.06f}},
        {{0.16f, 0.05f, 0.32f}},
        {{0.48f, 0.12f, 0.48f}},
        {{0.92f, 0.52f, 0.12f}},
        {{1.00f, 0.97f, 0.90f}},
    }};

    const float scaled = t * float(kStops.size() - 1u);
    const size_t index = std::min<size_t>(size_t(std::floor(scaled)), kStops.size() - 2u);
    const float localT = scaled - float(index);

    const auto &start = kStops[index];
    const auto &end = kStops[index + 1u];
    return {
        std::lerp(start[0], end[0], localT),
        std::lerp(start[1], end[1], localT),
        std::lerp(start[2], end[2], localT),
        1.0f,
    };
}

float blurKernelWeight(int distance, uint8_t radius)
{
    if (radius == 0u)
    {
        return distance == 0 ? 1.0f : 0.0f;
    }

    if (radius == 1u)
    {
        switch (distance)
        {
        case 0: return 2.0f;
        case 1: return 1.0f;
        default: return 0.0f;
        }
    }

    switch (distance)
    {
    case 0: return 6.0f;
    case 1: return 4.0f;
    case 2: return 1.0f;
    default: return 0.0f;
    }
}

std::vector<float> blurScalarImage(const std::vector<float> &values,
                                   uint16_t width, uint16_t height,
                                   uint8_t radius)
{
    if (radius == 0u || width == 0u || height == 0u || values.empty())
    {
        return values;
    }

    std::vector<float> horizontal(values.size(), 0.0f);
    std::vector<float> blurred(values.size(), 0.0f);
    const int blurRadius = std::min<int>(radius, 2);

    for (uint16_t y = 0; y < height; ++y)
    {
        for (uint16_t x = 0; x < width; ++x)
        {
            float weightedSum = 0.0f;
            float totalWeight = 0.0f;
            for (int dx = -blurRadius; dx <= blurRadius; ++dx)
            {
                const uint16_t sampleX = static_cast<uint16_t>(std::clamp<int>(int(x) + dx,
                                                                               0,
                                                                               int(width) - 1));
                const float weight = blurKernelWeight(std::abs(dx), radius);
                weightedSum += values[size_t(y) * size_t(width) + size_t(sampleX)] * weight;
                totalWeight += weight;
            }
            horizontal[size_t(y) * size_t(width) + size_t(x)] =
                totalWeight > 0.0f ? (weightedSum / totalWeight) : 0.0f;
        }
    }

    for (uint16_t y = 0; y < height; ++y)
    {
        for (uint16_t x = 0; x < width; ++x)
        {
            float weightedSum = 0.0f;
            float totalWeight = 0.0f;
            for (int dy = -blurRadius; dy <= blurRadius; ++dy)
            {
                const uint16_t sampleY = static_cast<uint16_t>(std::clamp<int>(int(y) + dy,
                                                                               0,
                                                                               int(height) - 1));
                const float weight = blurKernelWeight(std::abs(dy), radius);
                weightedSum += horizontal[size_t(sampleY) * size_t(width) + size_t(x)] * weight;
                totalWeight += weight;
            }
            blurred[size_t(y) * size_t(width) + size_t(x)] =
                totalWeight > 0.0f ? (weightedSum / totalWeight) : 0.0f;
        }
    }

    return blurred;
}

} // namespace

bool computeStructureFactorImage(const ParticleSystem &particleSystem,
                                 const SimulationBox &simulationBox,
                                 const StructureFactorSettings &settings,
                                 StructureFactorImage &image,
                                 std::string &error)
{
    error.clear();
    image = {};

    if (settings.previewSize == 0u)
    {
        error = "Preview size must be at least 1 pixel.";
        return false;
    }

    if (settings.maxModeX < 0 || settings.maxModeY < 0)
    {
        error = "Reciprocal-space mode limits must be non-negative.";
        return false;
    }

    if (simulationBox.shape() != SimulationBox::Shape::Rectangular)
    {
        error = "Structure factor rendering currently requires a rectangular simulation box.";
        return false;
    }

    const bx::Vec3 boxSize = simulationBox.size();
    const double lx = static_cast<double>(boxSize.x);
    const double ly = static_cast<double>(boxSize.y);
    const double lz = static_cast<double>(boxSize.z);
    if (!(lx > 1.0e-9) || !(ly > 1.0e-9))
    {
        error = "Simulation box extents Lx and Ly must both be positive.";
        return false;
    }
    if (settings.allowOutOfPlaneModes && !(lz > 1.0e-9))
    {
        error = "Simulation box extent Lz must be positive for view-aligned out-of-plane sampling.";
        return false;
    }

    std::vector<const Particle *> sampledParticles;
    if (!collectStructureFactorParticles(particleSystem, settings.useVisibleParticlesOnly,
                                         sampledParticles, error))
    {
        return false;
    }

    const auto startTime = std::chrono::steady_clock::now();

    const double stepX = (2.0 * kPi) / lx;
    const double stepY = (2.0 * kPi) / ly;
    const double stepZ = settings.allowOutOfPlaneModes ? (2.0 * kPi) / lz : 0.0;

    image.width = settings.previewSize;
    image.height = settings.previewSize;
    image.particleCount = sampledParticles.size();
    image.rgba8Pixels.resize(size_t(image.width) * size_t(image.height) * 4u);

    std::vector<ReciprocalIndex> pixelIndices(size_t(image.width) * size_t(image.height));
    std::unordered_map<ReciprocalIndex, float, ReciprocalIndexHash> sampledValues;
    sampledValues.reserve(pixelIndices.size());

    for (uint16_t pixelY = 0; pixelY < image.height; ++pixelY)
    {
        const float normalizedY = image.height > 1u
                                      ? float(pixelY) / float(image.height - 1u)
                                      : 0.5f;
        const float screenModeY = (0.5f - normalizedY) * 2.0f * float(settings.maxModeY);

        for (uint16_t pixelX = 0; pixelX < image.width; ++pixelX)
        {
            const float normalizedX = image.width > 1u
                                          ? float(pixelX) / float(image.width - 1u)
                                          : 0.5f;
            const float screenModeX =
                (normalizedX - 0.5f) * 2.0f * float(settings.maxModeX);

            const bx::Vec3 screenWaveVector{
                float(double(screenModeX) * stepX),
                float(double(screenModeY) * stepY),
                0.0f,
            };
            const bx::Vec3 boxWaveVector =
                rotateScreenWaveVectorToBoxFrame(settings.sceneRotation, screenWaveVector);

            ReciprocalIndex index;
            index.a = static_cast<int>(std::lround(double(boxWaveVector.x) / stepX));
            index.b = static_cast<int>(std::lround(double(boxWaveVector.y) / stepY));
            index.c = settings.allowOutOfPlaneModes
                          ? static_cast<int>(std::lround(double(boxWaveVector.z) / stepZ))
                          : 0;

            const size_t pixelIndex = size_t(pixelY) * size_t(image.width) + size_t(pixelX);
            pixelIndices[pixelIndex] = index;
            if (!sampledValues.contains(index))
            {
                sampledValues.emplace(index,
                                     structureFactorValueForIndex(sampledParticles, index,
                                                                  stepX, stepY, stepZ,
                                                                  settings.logScale));
            }
        }
    }

    float displayMin = std::numeric_limits<float>::max();
    float displayMax = std::numeric_limits<float>::lowest();
    bool foundRangeValue = false;
    for (const auto &[index, value] : sampledValues)
    {
        if (settings.suppressCentralPeak && isCentralPeak(index))
        {
            continue;
        }

        displayMin = std::min(displayMin, value);
        displayMax = std::max(displayMax, value);
        foundRangeValue = true;
    }

    if (!foundRangeValue)
    {
        displayMin = 0.0f;
        displayMax = 1.0f;
    }
    else if (displayMax - displayMin < 1.0e-6f)
    {
        displayMax = displayMin + 1.0f;
    }

    image.displayMin = displayMin;
    image.displayMax = displayMax;

    std::vector<float> normalizedValues(size_t(image.width) * size_t(image.height), 0.0f);
    for (uint16_t pixelY = 0; pixelY < image.height; ++pixelY)
    {
        for (uint16_t pixelX = 0; pixelX < image.width; ++pixelX)
        {
            const size_t sourceIndex = size_t(pixelY) * size_t(image.width) + size_t(pixelX);
            const ReciprocalIndex &index = pixelIndices[sourceIndex];
            float value = sampledValues[index];
            if (settings.suppressCentralPeak && isCentralPeak(index))
            {
                value = displayMin;
            }

            normalizedValues[sourceIndex] =
                std::clamp((value - displayMin) / (displayMax - displayMin), 0.0f, 1.0f);
        }
    }

    normalizedValues = blurScalarImage(normalizedValues, image.width, image.height,
                                       settings.blurRadius);

    const float colorRangeMin = std::clamp(settings.colorRangeMin, 0.0f, 0.99f);
    const float colorRangeMax = std::clamp(settings.colorRangeMax,
                                           colorRangeMin + 0.01f, 1.0f);

    for (uint16_t pixelY = 0; pixelY < image.height; ++pixelY)
    {
        for (uint16_t pixelX = 0; pixelX < image.width; ++pixelX)
        {
            const size_t sourceIndex = size_t(pixelY) * size_t(image.width) + size_t(pixelX);
            const float t = std::clamp((normalizedValues[sourceIndex] - colorRangeMin)
                                           / (colorRangeMax - colorRangeMin),
                                       0.0f, 1.0f);
            const std::array<float, 4> color = structureFactorColor(t);
            const size_t pixelIndex = sourceIndex * 4u;
            image.rgba8Pixels[pixelIndex + 0u] =
                static_cast<uint8_t>(std::lround(color[0] * 255.0f));
            image.rgba8Pixels[pixelIndex + 1u] =
                static_cast<uint8_t>(std::lround(color[1] * 255.0f));
            image.rgba8Pixels[pixelIndex + 2u] =
                static_cast<uint8_t>(std::lround(color[2] * 255.0f));
            image.rgba8Pixels[pixelIndex + 3u] =
                static_cast<uint8_t>(std::lround(color[3] * 255.0f));
        }
    }

    const auto endTime = std::chrono::steady_clock::now();
    image.computeMilliseconds =
        std::chrono::duration<float, std::milli>(endTime - startTime).count();
    return true;
}

bool buildStructureFactorGpuParticleData(const ParticleSystem &particleSystem,
                                         const StructureFactorSettings &settings,
                                         StructureFactorGpuParticleData &data,
                                         std::string &error)
{
    error.clear();
    data = {};

    std::vector<const Particle *> sampledParticles;
    if (!collectStructureFactorParticles(particleSystem, settings.useVisibleParticlesOnly,
                                         sampledParticles, error))
    {
        return false;
    }

    data.particleCount = sampledParticles.size();
    data.width = chooseParticleTextureSide(sampledParticles.size());
    data.height = static_cast<uint16_t>((sampledParticles.size() + data.width - 1u)
                                        / data.width);
    data.rgba32fPixels.assign(size_t(data.width) * size_t(data.height) * 4u, 0.0f);

    for (size_t particleIndex = 0; particleIndex < sampledParticles.size(); ++particleIndex)
    {
        const Particle &particle = *sampledParticles[particleIndex];
        const size_t pixelIndex = particleIndex * 4u;
        data.rgba32fPixels[pixelIndex + 0u] = particle.position.x;
        data.rgba32fPixels[pixelIndex + 1u] = particle.position.y;
        data.rgba32fPixels[pixelIndex + 2u] = particle.position.z;
        data.rgba32fPixels[pixelIndex + 3u] = 1.0f;
    }

    return true;
}
