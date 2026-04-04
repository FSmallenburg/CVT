#pragma once

#include "ParticleSystem.h"
#include "SimulationBox.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct StructureFactorSettings
{
    uint16_t previewSize = 192u;
    uint8_t blurRadius = 1u;
    float colorRangeMin = 0.0f;
    float colorRangeMax = 1.0f;
    int maxModeX = 40;
    int maxModeY = 40;
    bool logScale = true;
    bool suppressCentralPeak = true;
    bool useVisibleParticlesOnly = false;
    bool allowOutOfPlaneModes = false;
    std::array<float, 16> sceneRotation{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
};

struct StructureFactorImage
{
    uint16_t width = 0;
    uint16_t height = 0;
    size_t particleCount = 0;
    float computeMilliseconds = 0.0f;
    float displayMin = 0.0f;
    float displayMax = 0.0f;
    std::vector<uint8_t> rgba8Pixels;
};

struct StructureFactorGpuParticleData
{
    uint16_t width = 0;
    uint16_t height = 0;
    size_t particleCount = 0;
    std::vector<float> rgba32fPixels;
};

bool computeStructureFactorImage(const ParticleSystem &particleSystem,
                                 const SimulationBox &simulationBox,
                                 const StructureFactorSettings &settings,
                                 StructureFactorImage &image,
                                 std::string &error);
bool buildStructureFactorGpuParticleData(const ParticleSystem &particleSystem,
                                         const StructureFactorSettings &settings,
                                         StructureFactorGpuParticleData &data,
                                         std::string &error);
