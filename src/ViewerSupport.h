#pragma once

#include "ColorPalette.h"
#include "ParticleSystem.h"
#include "SimulationBox.h"
#include "StructureFactor.h"
#include "TrajectoryReader.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

/// Controls how particle colors are assigned each frame.
enum class ColorMode : uint16_t
{
    FileDefault = 0,  ///< Use the color specified in the trajectory file.
    PaletteCycle,     ///< Assign colors by cycling through the species palette.
    Uniform,          ///< Single color for all particles (optionally orientation-tinted).
    Orientation,      ///< Color encodes the particle's orientation vector.
    BondCount,        ///< Color encodes the number of nearest neighbors.
    ParticleSize,     ///< Color encodes particle size relative to mean ± σ.
    Count,
};

/// Selects which quantity is mapped to color in neighbor-analysis mode.
enum class AnalysisColorMode : uint8_t
{
    Disabled = 0,
    NeighborCount,
    BondOrientationalOrderMagnitude,
    BondOrientationalOrderPhase,
    BondOrientationalQLMagnitude,
    BondOrientationalQBarLMagnitude,
};

/// Selects the axes used for the 2-D bond-order scatter plot.
enum class BondOrderScatterMode : uint8_t
{
    RawAxesQ = 0,
    RawAxesQBar,
    PrincipalComponentsQ,
    PrincipalComponentsQBar,
};

/// Controls when the structure-factor image is recomputed.
enum class StructureFactorUpdateMode : uint8_t
{
    UpdateWhenStationary = 0, ///< Recompute once the camera/playback is idle.
    UpdateAlways,             ///< Recompute every rendered frame.
    ManualOnly,               ///< Only recompute when the user explicitly requests it.
};

struct ParticleSizeColorStats
{
    float mean = 0.0f;
    float standardDeviation = 0.0f;
};

struct ParticleColorStatsCache
{
    ParticleSizeColorStats stats{};
    size_t particleCount = 0u;
    ColorMode colorMode = ColorMode::FileDefault;
};

struct BondOrderScatterData
{
    std::vector<float> xValues;
    std::vector<float> yValues;
    std::vector<uint32_t> pointColors;
    std::vector<uint32_t> particleIds;
};

struct BondOrderScatterCache
{
    bool valid = false;
    BondOrderScatterMode mode = BondOrderScatterMode::PrincipalComponentsQBar;
    TrajectoryReader::Dimensionality dimensionality =
        TrajectoryReader::Dimensionality::ThreeDimensional;
    uint8_t xOrder = 4u;
    uint8_t yOrder = 6u;
    uint32_t dataRevision = 0u;
    std::array<bool, kParticlePaletteColorCount> enabledSpecies{};
    BondOrderScatterData data;
};

struct BondOrderScatterInteractionState
{
    bool dragActive = false;
    float dragStartX = 0.0f;
    float dragStartY = 0.0f;
};

struct ViewerState
{
    ViewerState()
    {
        bx::mtxIdentity(sceneRotation);
        particleTypeVisible.fill(true);
        bondOrderScatterTypeEnabled.fill(true);
        bondOrderScatterCache.enabledSpecies.fill(true);
        structureFactorIncludedSpecies.fill(true);
    }

    bool showUi = true;
    bool showStats = false;
    bool showBox = true;
    bool basicControlsDefaultOpen = false;
    bool patchRenderSystemsDirty = true;
    bool bondRenderSystemsDirty = true;
    bool nearestNeighborRenderSystemsDirty = true;
    bool polygonRenderSystemsDirty = true;
    bool mobilitySystemDirty = true;
    uint16_t renderViewportWidth = 0;
    uint16_t renderViewportHeight = 0;
    uint16_t uiPanelWidth = 0;
    double mouseX = 0.0;
    double mouseY = 0.0;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;
    double leftMousePressX = 0.0;
    double leftMousePressY = 0.0;
    bool leftMouseDown = false;
    bool leftDragActive = false;
    bool leftTranslateMode = false;
    bool rightMouseDown = false;
    float sceneRotation[16];
    bx::Vec3 particleTranslation{0.0f, 0.0f, 0.0f};
    // Frame/file navigation actions queued by UI/input callbacks.
    int pendingFrameStep = 0;
    int pendingFrameIndex = -1;
    bool jumpToFirstFrame = false;
    bool jumpToLastFrame = false;
    bool pendingOpenDroppedFile = false;
    std::string pendingOpenPath;
    std::string fileOpenStatusMessage;
    float orthoZoom = 1.0f;
    float currentFps = 0.0f;
    uint8_t lightingLevelIndex = 14u;
    float particleSizeScale = 1.0f;
    uint16_t particleResolution = 10u;
    bool wrapParticlesToBox = true;
    TrajectoryReader::Dimensionality fileDimensionality =
        TrajectoryReader::Dimensionality::ThreeDimensional;
    float neighborCutoffFactor = 1.3f;
    bool autoFindNeighbors = false;
    bool neighborAnalysisValid = false;
    bool neighborAnalysisPanelOpen = false;
    // Neighbor-analysis and FK actions.
    bool pendingFindNeighbors = false;
    bool pendingRefreshAnalysisResults = false;
    bool frankKasperBondsCached = false;
    bool frankKasperViewModeEnabled = false;
    bool frankKasperViewActivatedOnce = false;
    bool hideNonFrankKasperUnbonded = true;
    bool frankKasperAutoRecalculate = true;
    bool pendingActivateFrankKasperView = false;
    bool pendingToggleFrankKasperUnbondedVisibility = false;
    bool pendingRecalculateFrankKasperBonds = false;
    bool nearestNeighborModeEnabled = false;
    std::array<bool, kParticlePaletteColorCount> particleTypeVisible{};
    std::array<bool, kParticlePaletteColorCount> bondOrderScatterTypeEnabled{};
    uint8_t maxSeenParticleTypeIndex = 0u;
    uint16_t orderParameterCount = 0u;
    bool sizeDistributionUseVisibleOnly = true;
    uint16_t sizeDistributionBinCount = 32u;
    std::unordered_set<uint32_t> selectedIds;
    std::unordered_set<uint32_t> hiddenIds;
    std::unordered_set<uint32_t> frankKasperAutoHiddenIds;
    // Selection/visibility actions.
    bool pendingHideSelected = false;
    bool pendingRevealAll = false;
    bool pendingOverlapCheck = false;
    bool pendingInvertSelected = false;
    bool pendingSelectBonded = false;
    bool pendingDescribeSelection = false;
    bool pendingDescribeSelectedBondOrder = false;
    bool pendingAlignViewToSelection = false;
    bool pendingApplyParticleTypeVisibility = false;
    bool pendingDescribeVisibleCount = false;
    bool pendingUnselect = false;
    bool pendingIncreaseSphereResolution = false;
    bool pendingDecreaseSphereResolution = false;
    bool cutPlaneEnabled = false;
    float cutPlaneSceneZ = 0.0f;
    // Camera/cut-plane and picking actions.
    bool pendingEnableCutPlane = false;
    bool pendingDisableCutPlane = false;
    int pendingCutPlaneStep = 0;
    bool pendingPickRequest = false;
    uint16_t pendingPickX = 0;
    uint16_t pendingPickY = 0;
    bool pickBufferValid = false;
    bool pendingPickReadback = false;
    uint32_t pendingReadbackFrame = UINT32_MAX;
    uint32_t pickSceneRevision = 1;
    uint32_t pendingPickRevision = 0;
    uint32_t cachedPickRevision = 0;
    uint32_t lastPickedId = 0;
    bool pendingScreenshotRequest = false;
    ColorMode colorMode = ColorMode::FileDefault;
    AnalysisColorMode analysisColorMode = AnalysisColorMode::Disabled;
    BondOrderScatterMode bondOrderScatterMode = BondOrderScatterMode::PrincipalComponentsQBar;
    ParticleColorStatsCache particleColorStatsCache{};
    ParticleColorStatsCache mobilityColorStatsCache{};
    uint8_t bondOrientationalOrder = 6u;
    uint8_t bondOrderScatterXAxisOrder = 4u;
    uint8_t bondOrderScatterYAxisOrder = 6u;
    uint32_t bondOrderScatterDataRevision = 1u;
    BondOrderScatterInteractionState bondOrderScatterInteraction{};
    BondOrderScatterCache bondOrderScatterCache{};
    bool mobilityModeEnabled = false;
    bool bondModeEnabled = false;
    bool bondDiagramGeometryDirty = true;
    bool bondDiagramViewDirty = true;
    bool bondDiagramRenderRequested = false;
    float bondDiagramPointScale = 0.05f;
    bool structureFactorDirty = true;
    bool structureFactorPendingCompute = false;
    bool structureFactorPanelOpen = false;
    StructureFactorUpdateMode structureFactorUpdateMode =
        StructureFactorUpdateMode::UpdateAlways;
    bool structureFactorUseGpu = true;
    bool structureFactorSpecifyModeCount = false;
    bool structureFactorInteractionLowResActive = false;
    bool structureFactorLogScale = true;
    bool structureFactorSuppressCentralPeak = true;
    bool structureFactorUseVisibleParticlesOnly = false;
    std::array<bool, kParticlePaletteColorCount> structureFactorIncludedSpecies{};
    uint8_t structureFactorBlurRadius = 1u;
    float structureFactorColorRangeMin = 0.08f;
    float structureFactorColorRangeMax = 0.5f;
    float structureFactorMaxKTimesSigma = 25.0f;
    uint16_t structureFactorImageSize = 256u;
    int structureFactorMaxModeX = 100;
    int structureFactorMaxModeY = 100;
    uint32_t structureFactorDataRevision = 1u;
    uint32_t structureFactorComputeRevision = 1u;
    uint32_t structureFactorBatchModesPerStep = 32u;
    uint16_t structureFactorGpuBatchRowsPerStep = 16u;
    StructureFactorBatchState structureFactorBatchState{};
    bool hasPreviousFramePositions = false;
    std::vector<bx::Vec3> previousRawPositions;
};

struct PickResources
{
    bgfx::TextureHandle renderColorTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle readbackTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depthTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureFormat::Enum colorFormat = bgfx::TextureFormat::Count;
    uint16_t width = 0;
    uint16_t height = 0;
    std::vector<uint8_t> readbackData;
    bool enabled = false;
    std::string disableReason;
};

struct BondDiagramResources
{
    bgfx::TextureHandle colorTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depthTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    uint16_t width = 0;
    uint16_t height = 0;
    bool enabled = false;
    std::string disableReason;
};

struct StructureFactorResources
{
    bgfx::TextureHandle colorTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle intensityTexture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle intensityFrameBuffer = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle particleDataTexture = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle gpuProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle colorizeProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle particleDataSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle intensitySampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle params0Uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle params1Uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle params2Uniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle rotationUniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle batchParamsUniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle colorParamsUniform = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle colorRangeUniform = BGFX_INVALID_HANDLE;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t particleTextureWidth = 0;
    uint16_t particleTextureHeight = 0;
    bgfx::TextureFormat::Enum particleTextureFormat = bgfx::TextureFormat::Count;
    bool enabled = false;
    bool gpuPathInitialized = false;
    bool gpuPathAvailable = false;
    std::string disableReason;
    std::string statusText;
    float computeMilliseconds = 0.0f;
    size_t particleCount = 0;
    uint32_t particleDataRevision = 0u;
    bool hasLoggedRenderMode = false;
    bool lastRenderUsedGpu = false;
    bool lastRenderWasLowRes = false;
    uint16_t lastRenderSize = 0u;
    bool gpuBatchActive = false;
    uint32_t gpuBatchRevision = 0u;
    uint16_t gpuBatchNextRow = 0u;
    float gpuBatchAccumulatedMilliseconds = 0.0f;
};

float computeCutPlaneStep(const SimulationBox &simulationBox);
/// Clamps a raw mouse/window coordinate in [0, extent) to a uint16_t pick coordinate.
uint16_t clampPickCoordinate(double value, int extent);
/// Converts a window Y coordinate (origin top-left) to a pick-buffer Y coordinate
/// (origin bottom-left) by flipping relative to @p height.
uint16_t mapWindowYToPickY(uint16_t windowY, uint16_t height);
/// Decodes a particle ID from a 4-byte pick-buffer pixel, respecting @p colorFormat byte order.
uint32_t decodeParticleId(const uint8_t *pixel, bgfx::TextureFormat::Enum colorFormat);

/// Marks every helper render system (patches, bonds, neighbor lines, polygons,
/// mobility, …) as dirty so they are rebuilt on the next active frame.
void markAllHelperSystemsDirty(ViewerState &state);
/// Marks systems that depend on particle visibility (hidden/revealed particles).
void markVisibilityDependentHelperSystemsDirty(ViewerState &state);
/// Marks systems that depend on per-particle color (e.g. bond-order scatter plot).
void markColorDependentHelperSystemsDirty(ViewerState &state);
/// Marks bond-like systems (patch bonds, nearest-neighbor overlay) dirty.
void markBondLikeHelperSystemsDirty(ViewerState &state);
/// Marks the mobility-mode render system dirty.
void markMobilitySystemDirty(ViewerState &state);
/// Marks the nearest-neighbor line render system dirty.
void markNearestNeighborRenderSystemsDirty(ViewerState &state);
/// Marks the bond-diagram geometry dirty (forces a full mesh rebuild).
void markBondDiagramGeometryDirty(ViewerState &state);
/// Marks the bond-diagram view dirty (forces a re-render without a full rebuild).
void markBondDiagramViewDirty(ViewerState &state);
/// Marks the bond-order scatter-plot data cache invalid.
void markBondOrderScatterDataDirty(ViewerState &state);
/// Marks the structure-factor image dirty so it is recomputed.
void markStructureFactorDirty(ViewerState &state);
/// Returns true when @p mode allows the structure factor to update while the
/// simulation is playing (i.e. not ManualOnly).
bool structureFactorAllowsAutomaticUpdates(StructureFactorUpdateMode mode);
/// Returns true when @p mode allows the structure factor to update on camera
/// interaction (relevant for UpdateWhenStationary).
bool structureFactorAllowsInteractionUpdates(StructureFactorUpdateMode mode);
/// Marks the pick buffer dirty so it is re-rendered before the next pick read-back.
void markPickBufferDirty(ViewerState &state);
/// Returns true if the pick buffer has been rendered and its revision is current.
bool hasValidPickBuffer(const ViewerState &state);
/// Returns the number of ColorMode values that are available for the current
/// file type and dimensionality stored in @p state.
size_t availableColorModeCount(const ViewerState &state);
/// Clamps state.colorMode to the available count so it is never out of range.
void clampColorModeToAvailable(ViewerState &state);

/// Moves all particles in @p selectedIds to the hidden set and marks them
/// invisible. Returns true if at least one particle was hidden.
bool hideSelectedParticles(ParticleSystem &particleSystem,
                           std::unordered_set<uint32_t> &selectedIds,
                           std::unordered_set<uint32_t> &hiddenIds);
/// Toggles selection membership for all visible particles: previously
/// unselected particles become selected and vice versa.
void invertSelection(ParticleSystem &particleSystem,
                     std::unordered_set<uint32_t> &selectedIds);
/// Translates and recenters the scene so that the centroid of the selected
/// particles is at the camera focus. Returns false when nothing is selected.
bool alignViewToSelectedParticles(ViewerState &state,
                                  const ParticleSystem &particleSystem,
                                  const SimulationBox &simulationBox);
/// Makes all previously hidden particles visible again and clears @p hiddenIds.
/// Returns true if any particles were revealed.
bool revealAllParticles(ParticleSystem &particleSystem,
                        std::unordered_set<uint32_t> &hiddenIds);
/// Applies the @p hiddenIds set to @p particleSystem by marking those particles
/// invisible. Used after loading a new frame to restore the previous hide state.
bool applyHiddenParticles(ParticleSystem &particleSystem,
                          const std::unordered_set<uint32_t> &hiddenIds);
/// Converts a single-character particle-type label (e.g. 'A', 'B') to a
/// zero-based palette index.
uint8_t particleTypeIndex(char label);
/// Returns true if the particle type identified by @p typeLabel is currently
/// set to visible in @p state.
bool isParticleTypeVisible(const ViewerState &state, char typeLabel);
/// Updates state.maxSeenParticleTypeIndex from the type labels present in @p particleSystem.
void noteEncounteredParticleTypes(ViewerState &state, const ParticleSystem &particleSystem);
/// Applies per-type visibility flags and the @p hiddenIds set to @p particleSystem.
/// Returns true if the visible set changed.
bool applyParticleVisibilityFilters(ParticleSystem &particleSystem,
                                    const ViewerState &state);

/// Reads the pick-buffer pixel at the pending pick coordinates, decodes the
/// particle ID, stores it in state.lastPickedId, and clears the request.
void resolvePendingPickRequest(ViewerState &state, const PickResources &pickResources);

/// Releases all bgfx handles held by @p pickResources and resets their values.
void destroyPickResources(PickResources &pickResources);
/// Allocates colour, depth, and read-back textures plus a frame buffer for
/// off-screen particle picking at resolution @p width × @p height.
/// Returns false if creation fails (sets pickResources.disableReason).
bool createPickResources(PickResources &pickResources, uint16_t width, uint16_t height);

/// Releases all bgfx handles held by @p bondDiagramResources.
void destroyBondDiagramResources(BondDiagramResources &bondDiagramResources);
/// Creates the off-screen frame buffer used to render the bond-order diagram.
bool createBondDiagramResources(BondDiagramResources &bondDiagramResources,
                                uint16_t width, uint16_t height);

/// Releases all bgfx handles held by @p structureFactorResources.
void destroyStructureFactorResources(StructureFactorResources &structureFactorResources);
/// Creates (or recreates) the render targets used for structure-factor rendering at
/// resolution @p width × @p height. Returns false on failure.
bool createStructureFactorRenderTarget(StructureFactorResources &structureFactorResources,
                                       uint16_t width, uint16_t height);
/// Uploads new RGBA-8 pixel data to the structure-factor display texture,
/// resizing it if the dimensions changed. Returns false on failure.
bool updateStructureFactorTexture(StructureFactorResources &structureFactorResources,
                                  uint16_t width, uint16_t height,
                                  const std::vector<uint8_t> &rgba8Pixels);

/// Returns a reasonable initial camera-to-scene distance for @p simulationBox,
/// large enough that the whole box is visible.
float computeInitialCameraDistance(const SimulationBox &simulationBox);
/// Returns a suitable far-plane distance given the initial camera distance and
/// the box size, with a generous margin.
float computeInitialFarPlane(float cameraDistance, const SimulationBox &simulationBox);
/// Returns an orthographic half-height that fits the simulation box into the
/// viewport at the given @p aspectRatio.
float computeInitialOrthoHalfHeight(const SimulationBox &simulationBox, float aspectRatio);
/// Applies incremental Euler-angle rotations (@p angleX, @p angleY, @p angleZ
/// in radians) to state.sceneRotation.
void applySceneRotation(ViewerState &state, float angleX, float angleY, float angleZ);