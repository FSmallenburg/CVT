#pragma once

#include "ColorPalette.h"
#include "ParticleSystem.h"
#include "SimulationBox.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct ViewerState;
struct StructureFactorResources;

using StructureFactorShaderLoader = bgfx::ShaderHandle (*)(const char *path);

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
    std::array<bool, kParticlePaletteColorCount> includedSpecies{};
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

struct StructureFactorModeIndex
{
    int a = 0;
    int b = 0;
    int c = 0;
};

struct StructureFactorBatchState
{
    bool active = false;
    uint32_t computeRevision = 0u;
    uint16_t width = 0;
    uint16_t height = 0;
    size_t particleCount = 0u;
    double stepX = 0.0;
    double stepY = 0.0;
    double stepZ = 0.0;
    std::vector<std::array<float, 3>> sampledPositions;
    std::vector<StructureFactorModeIndex> uniqueModes;
    std::vector<uint32_t> pixelModeIndices;
    std::vector<float> modeValues;
    uint32_t nextModeIndex = 0u;
    float accumulatedComputeMilliseconds = 0.0f;
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
bool beginStructureFactorBatch(const ParticleSystem &particleSystem,
                               const SimulationBox &simulationBox,
                               const StructureFactorSettings &settings,
                               uint32_t computeRevision,
                               StructureFactorBatchState &batch,
                               std::string &error);
bool advanceStructureFactorBatch(const StructureFactorSettings &settings,
                                 uint32_t modesPerStep,
                                 StructureFactorBatchState &batch,
                                 StructureFactorImage &image,
                                 bool &finished,
                                 float &stepMilliseconds,
                                 std::string &error);

void updateStructureFactorPreview(ViewerState &viewerState,
                                  const SimulationBox &simulationBox,
                                  const ParticleSystem &particleSystem,
                                  StructureFactorResources &structureFactorResources,
                                  StructureFactorShaderLoader shaderLoader);
