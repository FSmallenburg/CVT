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

enum class ColorMode : uint8_t
{
    FileDefault = 0,
    PaletteCycle,
    Uniform,
    Orientation,
    BondCount,
    Count,
};

struct ViewerState
{
    ViewerState()
    {
        bx::mtxIdentity(sceneRotation);
        particleTypeVisible.fill(true);
    }

    bool showUi = true;
    bool showStats = false;
    bool showBox = true;
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
    float orthoZoom = 1.0f;
    uint8_t lightingLevelIndex = 14u;
    float particleSizeScale = 1.0f;
    bool wrapParticlesToBox = true;
    TrajectoryReader::Dimensionality fileDimensionality =
        TrajectoryReader::Dimensionality::ThreeDimensional;
    std::array<bool, kParticlePaletteColorCount> particleTypeVisible{};
    uint8_t maxSeenParticleTypeIndex = 0u;
    std::unordered_set<uint32_t> selectedIds;
    std::unordered_set<uint32_t> hiddenIds;
    bool pendingHideSelected = false;
    bool pendingRevealAll = false;
    bool pendingOverlapCheck = false;
    bool pendingInvertSelected = false;
    bool pendingSelectBonded = false;
    bool pendingDescribeSelection = false;
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
    bool mobilityModeEnabled = false;
    bool bondModeEnabled = false;
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

float computeCutPlaneStep(const SimulationBox &simulationBox);
uint16_t clampPickCoordinate(double value, int extent);
uint16_t mapWindowYToPickY(uint16_t windowY, uint16_t height);
uint32_t decodeParticleId(const uint8_t *pixel, bgfx::TextureFormat::Enum colorFormat);
void markPickBufferDirty(ViewerState &state);
bool hasValidPickBuffer(const ViewerState &state);
bool hideSelectedParticles(ParticleSystem &particleSystem,
                           std::unordered_set<uint32_t> &selectedIds,
                           std::unordered_set<uint32_t> &hiddenIds);
void invertSelection(ParticleSystem &particleSystem,
                     std::unordered_set<uint32_t> &selectedIds);
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
float computeInitialCameraDistance(const SimulationBox &simulationBox);
float computeInitialFarPlane(float cameraDistance, const SimulationBox &simulationBox);
float computeInitialOrthoHalfHeight(const SimulationBox &simulationBox, float aspectRatio);
void applySceneRotation(ViewerState &state, float angleX, float angleY, float angleZ);