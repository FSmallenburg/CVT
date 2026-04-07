#pragma once

#include "ColorPalette.h"
#include "ParticleSystem.h"
#include "SimulationBox.h"
#include "TrajectoryReader.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

enum class ColorMode : uint16_t
{
    FileDefault = 0,
    PaletteCycle,
    Uniform,
    Orientation,
    BondCount,
    ParticleSize,
    Count,
};

enum class AnalysisColorMode : uint8_t
{
    Disabled = 0,
    NeighborCount,
    BondOrientationalOrderMagnitude,
    BondOrientationalOrderPhase,
    BondOrientationalQLMagnitude,
    BondOrientationalQBarLMagnitude,
};

enum class BondOrderScatterMode : uint8_t
{
    RawAxesQ = 0,
    RawAxesQBar,
    PrincipalComponentsQ,
    PrincipalComponentsQBar,
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
    }

    bool showUi = true;
    bool showStats = false;
    bool showBox = true;
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
    bool wrapParticlesToBox = true;
    TrajectoryReader::Dimensionality fileDimensionality =
        TrajectoryReader::Dimensionality::ThreeDimensional;
    float neighborCutoffFactor = 1.3f;
    bool autoFindNeighbors = false;
    bool neighborAnalysisValid = false;
    bool pendingFindNeighbors = false;
    bool pendingRefreshAnalysisResults = false;
    bool nearestNeighborModeEnabled = false;
    std::array<bool, kParticlePaletteColorCount> particleTypeVisible{};
    std::array<bool, kParticlePaletteColorCount> bondOrderScatterTypeEnabled{};
    uint8_t maxSeenParticleTypeIndex = 0u;
    uint16_t orderParameterCount = 0u;
    bool sizeDistributionUseVisibleOnly = true;
    uint16_t sizeDistributionBinCount = 32u;
    std::unordered_set<uint32_t> selectedIds;
    std::unordered_set<uint32_t> hiddenIds;
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
    bool structureFactorAutoUpdate = true;
    bool structureFactorUseGpu = true;
    bool structureFactorSpecifyModeCount = false;
    bool structureFactorInteractionLowResActive = false;
    bool structureFactorLogScale = true;
    bool structureFactorSuppressCentralPeak = true;
    bool structureFactorUseVisibleParticlesOnly = false;
    uint8_t structureFactorBlurRadius = 1u;
    float structureFactorColorRangeMin = 0.08f;
    float structureFactorColorRangeMax = 0.5f;
    float structureFactorMaxKTimesSigma = 25.0f;
    uint16_t structureFactorImageSize = 256u;
    int structureFactorMaxModeX = 100;
    int structureFactorMaxModeY = 100;
    uint32_t structureFactorDataRevision = 1u;
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
};

float computeCutPlaneStep(const SimulationBox &simulationBox);
uint16_t clampPickCoordinate(double value, int extent);
uint16_t mapWindowYToPickY(uint16_t windowY, uint16_t height);
uint32_t decodeParticleId(const uint8_t *pixel, bgfx::TextureFormat::Enum colorFormat);
void markAllHelperSystemsDirty(ViewerState &state);
void markVisibilityDependentHelperSystemsDirty(ViewerState &state);
void markColorDependentHelperSystemsDirty(ViewerState &state);
void markBondLikeHelperSystemsDirty(ViewerState &state);
void markMobilitySystemDirty(ViewerState &state);
void markNearestNeighborRenderSystemsDirty(ViewerState &state);
void markBondDiagramGeometryDirty(ViewerState &state);
void markBondDiagramViewDirty(ViewerState &state);
void markBondOrderScatterDataDirty(ViewerState &state);
void markStructureFactorDirty(ViewerState &state);
void markPickBufferDirty(ViewerState &state);
bool hasValidPickBuffer(const ViewerState &state);
size_t availableColorModeCount(const ViewerState &state);
void clampColorModeToAvailable(ViewerState &state);
bool hideSelectedParticles(ParticleSystem &particleSystem,
                           std::unordered_set<uint32_t> &selectedIds,
                           std::unordered_set<uint32_t> &hiddenIds);
void invertSelection(ParticleSystem &particleSystem,
                     std::unordered_set<uint32_t> &selectedIds);
bool alignViewToSelectedParticles(ViewerState &state,
                                  const ParticleSystem &particleSystem,
                                  const SimulationBox &simulationBox);
bool revealAllParticles(ParticleSystem &particleSystem,
                        std::unordered_set<uint32_t> &hiddenIds);
bool applyHiddenParticles(ParticleSystem &particleSystem,
                          const std::unordered_set<uint32_t> &hiddenIds);
uint8_t particleTypeIndex(char label);
bool isParticleTypeVisible(const ViewerState &state, char typeLabel);
void noteEncounteredParticleTypes(ViewerState &state, const ParticleSystem &particleSystem);
bool applyParticleVisibilityFilters(ParticleSystem &particleSystem,
                                    const ViewerState &state);
void resolvePendingPickRequest(ViewerState &state, const PickResources &pickResources);
void destroyPickResources(PickResources &pickResources);
bool createPickResources(PickResources &pickResources, uint16_t width, uint16_t height);
void destroyBondDiagramResources(BondDiagramResources &bondDiagramResources);
bool createBondDiagramResources(BondDiagramResources &bondDiagramResources,
                                uint16_t width, uint16_t height);
void destroyStructureFactorResources(StructureFactorResources &structureFactorResources);
bool createStructureFactorRenderTarget(StructureFactorResources &structureFactorResources,
                                       uint16_t width, uint16_t height);
bool updateStructureFactorTexture(StructureFactorResources &structureFactorResources,
                                  uint16_t width, uint16_t height,
                                  const std::vector<uint8_t> &rgba8Pixels);
float computeInitialCameraDistance(const SimulationBox &simulationBox);
float computeInitialFarPlane(float cameraDistance, const SimulationBox &simulationBox);
float computeInitialOrthoHalfHeight(const SimulationBox &simulationBox, float aspectRatio);
void applySceneRotation(ViewerState &state, float angleX, float angleY, float angleZ);