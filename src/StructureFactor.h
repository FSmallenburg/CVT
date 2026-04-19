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

/// Function pointer type used to load a bgfx shader by file path.
/// Injected at call sites so the structure-factor module doesn't depend on
/// the app's shader-loading path directly.
using StructureFactorShaderLoader = bgfx::ShaderHandle (*)(const char *path);

/// All tunable parameters for a single structure-factor computation or render.
struct StructureFactorSettings
{
    uint16_t previewSize = 192u;        ///< Output image resolution in pixels (square).
    uint8_t  blurRadius  = 1u;          ///< Gaussian blur applied to the computed image.
    float colorRangeMin  = 0.0f;        ///< Minimum intensity mapped to the colour palette.
    float colorRangeMax  = 1.0f;        ///< Maximum intensity mapped to the colour palette.
    int maxModeX = 40;                  ///< Maximum \|kx\| mode index to include.
    int maxModeY = 40;                  ///< Maximum \|ky\| mode index to include.
    bool logScale               = true; ///< Apply log10 to intensities before colouring.
    bool suppressCentralPeak    = true; ///< Zero out the k=0 peak.
    bool useVisibleParticlesOnly = false;
    bool allowOutOfPlaneModes   = false; ///< Include kz ≠ 0 modes (3-D files only).
    std::array<bool, kParticlePaletteColorCount> includedSpecies{};
    /// Column-major 4×4 scene rotation matrix used to orient the reciprocal lattice.
    std::array<float, 16> sceneRotation{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
};

/// The result of a completed structure-factor computation.
struct StructureFactorImage
{
    uint16_t width  = 0;
    uint16_t height = 0;
    size_t particleCount = 0;
    float computeMilliseconds = 0.0f;
    float displayMin = 0.0f;  ///< Actual min intensity value after normalisation.
    float displayMax = 0.0f;  ///< Actual max intensity value after normalisation.
    std::vector<uint8_t> rgba8Pixels; ///< Row-major RGBA pixel data, ready to upload.
};

/// Intermediate particle data packed for GPU-accelerated structure-factor computation.
struct StructureFactorGpuParticleData
{
    uint16_t width  = 0;
    uint16_t height = 0;
    size_t particleCount = 0;
    /// RGBA32F texture data (x=posX, y=posY, z=posZ, w=weight) for each particle.
    std::vector<float> rgba32fPixels;
};

/// Integer reciprocal-lattice mode index (a, b, c) for a single S(k) sample.
struct StructureFactorModeIndex
{
    int a = 0;
    int b = 0;
    int c = 0;
};

/// Persistent state for an incremental (batched) CPU structure-factor computation.
/// Allows the calculation to be spread over multiple frames to avoid stalls.
struct StructureFactorBatchState
{
    bool active = false;
    /// Revision token; mismatching the ViewerState revision cancels the batch.
    uint32_t computeRevision = 0u;
    uint16_t width  = 0;
    uint16_t height = 0;
    size_t particleCount = 0u;
    double stepX = 0.0; ///< Reciprocal-lattice step size along X.
    double stepY = 0.0;
    double stepZ = 0.0;
    std::vector<std::array<float, 3>> sampledPositions;
    std::vector<StructureFactorModeIndex> uniqueModes;  ///< All k-modes to evaluate.
    std::vector<uint32_t> pixelModeIndices;             ///< Maps each output pixel to a mode.
    std::vector<float> modeValues;                      ///< Accumulated S(k) value per mode.
    uint32_t nextModeIndex = 0u;                        ///< Next mode to process.
    float accumulatedComputeMilliseconds = 0.0f;
};

/// Computes the full structure-factor image on the CPU in a single call.
/// Returns false and sets @p error on failure.
bool computeStructureFactorImage(const ParticleSystem &particleSystem,
                                 const SimulationBox &simulationBox,
                                 const StructureFactorSettings &settings,
                                 StructureFactorImage &image,
                                 std::string &error);

/// Packs particle positions and weights into @p data ready for GPU upload.
/// Returns false and sets @p error on failure.
bool buildStructureFactorGpuParticleData(const ParticleSystem &particleSystem,
                                         const StructureFactorSettings &settings,
                                         StructureFactorGpuParticleData &data,
                                         std::string &error);

/// Initialises a new incremental CPU batch. Must be called before
/// advanceStructureFactorBatch(). Returns false and sets @p error on failure.
bool beginStructureFactorBatch(const ParticleSystem &particleSystem,
                               const SimulationBox &simulationBox,
                               const StructureFactorSettings &settings,
                               uint32_t computeRevision,
                               StructureFactorBatchState &batch,
                               std::string &error);

/// Processes up to @p modesPerStep modes of the ongoing batch. Sets
/// @p finished to true when all modes have been evaluated and @p image has
/// been fully populated. Returns false and sets @p error on failure.
bool advanceStructureFactorBatch(const StructureFactorSettings &settings,
                                 uint32_t modesPerStep,
                                 StructureFactorBatchState &batch,
                                 StructureFactorImage &image,
                                 bool &finished,
                                 float &stepMilliseconds,
                                 std::string &error);

/// High-level per-frame update called from the main loop. Decides whether to
/// use the GPU path or the incremental CPU batch, advances progress, and
/// uploads the result to @p structureFactorResources when complete.
void updateStructureFactorPreview(ViewerState &viewerState,
                                  const SimulationBox &simulationBox,
                                  const ParticleSystem &particleSystem,
                                  StructureFactorResources &structureFactorResources,
                                  StructureFactorShaderLoader shaderLoader);
