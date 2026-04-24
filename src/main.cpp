#include "AnalysisSupport.h"
#include "AppBootstrap.h"
#include "ArrowType.h"
#include "ViewerConfig.h"
#include "ColorPalette.h"
#include "CubeType.h"
#include "CylinderType.h"
#include "ImGuiBgfx.h"
#include "Log.h"
#include "Mesh.h"
#include "OctahedronType.h"
#include "PatchCapType.h"
#include "PatchConeType.h"
#include "PatchPlacement.h"
#include "Particle.h"
#include "ParticleSystem.h"
#include "PolygonType.h"
#include "RodType.h"
#include "ScreenshotSupport.h"
#include "SceneRenderSupport.h"
#include "SimulationBox.h"
#include "SphereType.h"
#include "StructureFactor.h"
#include "TrajectoryReader.h"
#include "ViewerSupport.h"
#include "ViewerUi.h"

#include <bx/bx.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>
#include <dear-imgui/imgui.h>
#include <GLFW/glfw3.h>

#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
#include <unistd.h>
#include <limits.h>
#elif BX_PLATFORM_OSX
#include <mach-o/dyld.h>
#elif BX_PLATFORM_WINDOWS
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <complex>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

static bool openTrajectoryFile(const std::string &path,
                               ViewerState &viewerState,
                               const bgfx::VertexLayout &layout,
                               uint16_t sphereStacks, uint16_t sphereSlices,
                               std::unique_ptr<TrajectoryReader> &trajectoryReader,
                               ParticleSystem &particleSystem,
                               TrajectoryReader::FileType &particleFileType,
                               SimulationBox &simulationBox,
                               StructureFactorResources &structureFactorResources,
                               std::string &loadedPath,
                               size_t &currentFrame,
                               size_t &totalFrames);

namespace
{

std::vector<fs::path> s_resourceSearchRoots;

fs::path fallbackExecutablePath(const char *argv0)
{
    if (argv0 == nullptr || argv0[0] == '\0')
    {
        return {};
    }

    std::error_code error;
    fs::path path(argv0);
    if (path.is_relative())
    {
        path = fs::absolute(path, error);
        if (error)
        {
            return {};
        }
    }

    const fs::path canonicalPath = fs::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonicalPath;
}

fs::path currentExecutablePath(const char *argv0)
{
#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
    char buffer[PATH_MAX];
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length > 0)
    {
        buffer[length] = '\0';
        return fs::path(buffer);
    }
#elif BX_PLATFORM_OSX
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0)
    {
        std::error_code error;
        const fs::path path(buffer.data());
        const fs::path canonicalPath = fs::weakly_canonical(path, error);
        return error ? path.lexically_normal() : canonicalPath;
    }
#elif BX_PLATFORM_WINDOWS
    std::vector<wchar_t> buffer(MAX_PATH);
    DWORD length = 0;
    while (true)
    {
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            break;
        }
        if (length < buffer.size())
        {
            return fs::path(buffer.data(), buffer.data() + length);
        }
        buffer.resize(buffer.size() * 2u);
    }
#endif

    return fallbackExecutablePath(argv0);
}

void addSearchRoot(std::vector<fs::path> &roots, const fs::path &root)
{
    if (root.empty())
    {
        return;
    }

    std::error_code error;
    const fs::path normalizedRoot = fs::weakly_canonical(root, error);
    const fs::path finalRoot = error ? root.lexically_normal() : normalizedRoot;
    if (std::find(roots.begin(), roots.end(), finalRoot) == roots.end())
    {
        roots.push_back(finalRoot);
    }
}

std::vector<fs::path> buildResourceSearchRoots(const char *argv0)
{
    std::vector<fs::path> roots;

    std::error_code error;
    addSearchRoot(roots, fs::current_path(error));

    const fs::path executablePath = currentExecutablePath(argv0);
    if (!executablePath.empty())
    {
        const fs::path executableDir = executablePath.parent_path();
        addSearchRoot(roots, executableDir);
        addSearchRoot(roots, executableDir.parent_path());
#if BX_PLATFORM_OSX
        addSearchRoot(roots, executableDir.parent_path() / "Resources");
#endif
    }

    return roots;
}

fs::path resolveResourcePath(const fs::path &relativePath)
{
    std::error_code error;
    if (relativePath.is_absolute() && fs::exists(relativePath, error))
    {
        return relativePath;
    }

    for (const fs::path &root : s_resourceSearchRoots)
    {
        const fs::path candidate = root / relativePath;
        if (fs::exists(candidate, error))
        {
            return candidate;
        }
        error.clear();
    }

    return {};
}

const char *shaderOutputDirectoryForRenderer(bgfx::RendererType::Enum rendererType)
{
    switch (rendererType)
    {
    case bgfx::RendererType::Direct3D11:
        return "dxbc";
    case bgfx::RendererType::Metal:
        return "metal";
    case bgfx::RendererType::OpenGL:
        return "glsl";
    default:
        return nullptr;
    }
}

fs::path resolveShaderBinaryPath(const char *path)
{
    const fs::path relativePath(path);
    const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
    if (const char *shaderDirectory = shaderOutputDirectoryForRenderer(rendererType))
    {
        const fs::path rendererSpecificPath =
            relativePath.parent_path() / shaderDirectory / relativePath.filename();
        const fs::path resolvedRendererSpecificPath =
            resolveResourcePath(rendererSpecificPath);
        if (!resolvedRendererSpecificPath.empty())
        {
            return resolvedRendererSpecificPath;
        }

        if (rendererType != bgfx::RendererType::OpenGL)
        {
            return rendererSpecificPath;
        }
    }

    const fs::path resolvedPath = resolveResourcePath(relativePath);
    return resolvedPath.empty() ? relativePath : resolvedPath;
}

bool initializeBgfxRenderer(bgfx::Init &init)
{
#if BX_PLATFORM_WINDOWS
    constexpr std::array<bgfx::RendererType::Enum, 2> kRendererPreference = {
        bgfx::RendererType::OpenGL,
        bgfx::RendererType::Direct3D11,
    };
#elif BX_PLATFORM_OSX
    constexpr std::array<bgfx::RendererType::Enum, 2> kRendererPreference = {
        bgfx::RendererType::Metal,
        bgfx::RendererType::OpenGL,
    };
#else
    constexpr std::array<bgfx::RendererType::Enum, 1> kRendererPreference = {
        bgfx::RendererType::OpenGL,
    };
#endif

    bgfx::RendererType::Enum supportedRenderers[bgfx::RendererType::Count];
    const uint8_t supportedRendererCount =
        bgfx::getSupportedRenderers(BX_COUNTOF(supportedRenderers), supportedRenderers);

    for (bgfx::RendererType::Enum rendererType : kRendererPreference)
    {
        const bool rendererSupported =
            std::find(supportedRenderers, supportedRenderers + supportedRendererCount,
                      rendererType)
            != (supportedRenderers + supportedRendererCount);
        if (!rendererSupported)
        {
            cvt::log::warn() << "bgfx renderer backend unavailable: "
                             << bgfx::getRendererName(rendererType) << std::endl;
            continue;
        }

        init.type = rendererType;
        if (bgfx::init(init))
        {
            cvt::log::info() << "Using bgfx renderer backend: "
                             << bgfx::getRendererName(bgfx::getRendererType()) << std::endl;
            return true;
        }

        cvt::log::error() << "Failed to initialize bgfx renderer backend: "
                          << bgfx::getRendererName(rendererType) << std::endl;
    }

    return false;
}

} // namespace

// ---- Load shader ----
bgfx::ShaderHandle loadShader(const char *path)
{
    const fs::path shaderPath = resolveShaderBinaryPath(path);
    std::ifstream file(shaderPath, std::ios::binary);
    if (!file)
    {
        cvt::log::error() << "Failed to open shader: " << path;
        if (!s_resourceSearchRoots.empty())
        {
            cvt::log::error() << " (searched";
            for (const fs::path &root : s_resourceSearchRoots)
            {
                cvt::log::error() << ' ' << (root / path).string();
            }
            cvt::log::error() << ')';
        }
        cvt::log::error() << std::endl;
        return BGFX_INVALID_HANDLE;
    }
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0);

    const bgfx::Memory *mem = bgfx::alloc(size);
    file.read(reinterpret_cast<char *>(mem->data), static_cast<std::streamsize>(size));

    return bgfx::createShader(mem);
}

static const bx::Vec3 s_cameraTarget = {0.0f, 0.0f, 0.0f};
constexpr bx::Vec3 kInitialLightDirection = {0.0f, 0.0f, 5.0f};
// World-space direction from the shaded point toward the light source.

namespace
{

constexpr float kZoomPerWheelStep = 0.9f;
constexpr float kMinOrthoZoom = 0.05f;
constexpr float kMaxOrthoZoom = 100.0f;
constexpr float kMouseRotationScale = 0.005f;
constexpr float kMouseTranslationScale = 2.0f;
constexpr double kClickDragThresholdPixels = 4.0;
constexpr bgfx::ViewId kMainView = 0;
constexpr bgfx::ViewId kPickView = 1;
constexpr bgfx::ViewId kBlitView = 2;
constexpr bgfx::ViewId kBondDiagramView = 3;
constexpr bgfx::ViewId kUiView = 4;
constexpr uint16_t kDefaultArrowSlices = 12;
constexpr uint16_t kDefaultSphereStacks = 10;
constexpr uint16_t kDefaultSphereSlices = 10;
constexpr uint16_t kMinSphereStacks = 4;
constexpr uint16_t kMinSphereSlices = 4;
constexpr uint16_t kSphereResolutionStep = 2;
constexpr float kParticleSizeScaleStep = 0.01f;
constexpr float kMinParticleSizeScale = 0.01f;
constexpr uint8_t kLightingLevelCount = 30u;
constexpr uint16_t kBondDiagramPreviewSize = 192u;
constexpr size_t kLargeStructureFactorParticleThreshold = 25000u;
constexpr uint16_t kBondDiagramSphereStacks = 10u;
constexpr uint16_t kBondDiagramSphereSlices = 10u;
constexpr uint8_t kFrankKasperNeighborCountNoHide = 12u;
float s_uiScrollX = 0.0f;
float s_uiScrollY = 0.0f;

struct LineVertex
{
    float x;
    float y;
    float z;
    uint32_t abgr;

    static void init(bgfx::VertexLayout &layout)
    {
        layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();
    }
};

int particleTypeHotkeyIndex(int key)
{
    switch (key)
    {
    case GLFW_KEY_1:
    case GLFW_KEY_KP_1:
        return 0;
    case GLFW_KEY_2:
    case GLFW_KEY_KP_2:
        return 1;
    case GLFW_KEY_3:
    case GLFW_KEY_KP_3:
        return 2;
    case GLFW_KEY_4:
    case GLFW_KEY_KP_4:
        return 3;
    case GLFW_KEY_5:
    case GLFW_KEY_KP_5:
        return 4;
    case GLFW_KEY_6:
    case GLFW_KEY_KP_6:
        return 5;
    case GLFW_KEY_7:
    case GLFW_KEY_KP_7:
        return 6;
    case GLFW_KEY_8:
    case GLFW_KEY_KP_8:
        return 7;
    case GLFW_KEY_9:
    case GLFW_KEY_KP_9:
        return 8;
    case GLFW_KEY_0:
    case GLFW_KEY_KP_0:
        return 9;
    default:
        return -1;
    }
}

bx::Vec3 inverseRotateVector(const float *rotationTransform, const bx::Vec3 &vector)
{
    return {
        vector.x * rotationTransform[0] + vector.y * rotationTransform[1]
            + vector.z * rotationTransform[2],
        vector.x * rotationTransform[4] + vector.y * rotationTransform[5]
            + vector.z * rotationTransform[6],
        vector.x * rotationTransform[8] + vector.y * rotationTransform[9]
            + vector.z * rotationTransform[10],
    };
}


void snapshotCurrentParticlePositions(const ParticleSystem &particleSystem,
                                      ViewerState &viewerState, bool hasPreviousFrame)
{
    viewerState.previousRawPositions.clear();
    viewerState.previousRawPositions.reserve(particleSystem.particles().size());
    for (const Particle &particle : particleSystem.particles())
    {
        viewerState.previousRawPositions.push_back(particle.position);
    }
    viewerState.hasPreviousFramePositions = hasPreviousFrame;
}

float particleRadius(const Particle &particle)
{
    return particle.sizeParams[0];
}

float scaledParticleRadius(const Particle &particle, float particleSizeScale)
{
    return particleSizeScale * particleRadius(particle);
}

void findOverlappingSphereParticles(const ParticleSystem &particleSystem,
                                    const SimulationBox &simulationBox,
                                    float particleSizeScale,
                                    std::unordered_set<uint32_t> &selectedIds)
{
    selectedIds.clear();

    const std::vector<Particle> &particles = particleSystem.particles();
    for (size_t firstIndex = 0; firstIndex < particles.size(); ++firstIndex)
    {
        const Particle &firstParticle = particles[firstIndex];
        if (!firstParticle.visible)
        {
            continue;
        }

        for (size_t secondIndex = firstIndex + 1; secondIndex < particles.size(); ++secondIndex)
        {
            const Particle &secondParticle = particles[secondIndex];
            if (!secondParticle.visible)
            {
                continue;
            }

            bx::Vec3 displacement = {
                secondParticle.position.x - firstParticle.position.x,
                secondParticle.position.y - firstParticle.position.y,
                secondParticle.position.z - firstParticle.position.z,
            };
            displacement = simulationBox.nearestImage(displacement);

            const float overlapDistance = scaledParticleRadius(firstParticle, particleSizeScale)
                                          + scaledParticleRadius(secondParticle,
                                                                 particleSizeScale);
            if (bx::length(displacement) < overlapDistance)
            {
                selectedIds.insert(firstParticle.id);
                selectedIds.insert(secondParticle.id);
            }
        }
    }
}

const Particle *findParticleById(const ParticleSystem &particleSystem, uint32_t particleId)
{
    const std::vector<Particle> &particles = particleSystem.particles();
    const auto particleIt = std::find_if(
        particles.begin(), particles.end(),
        [particleId](const Particle &particle) { return particle.id == particleId; });
    return particleIt != particles.end() ? &(*particleIt) : nullptr;
}

void printSelectedParticles(const ParticleSystem &particleSystem,
                            const std::unordered_set<uint32_t> &selectedIds,
                            const SimulationBox &simulationBox)
{
    const auto displayParticleId = [](uint32_t particleId) {
        return particleId > 0u ? (particleId - 1u) : 0u;
    };

    std::vector<uint32_t> sortedIds(selectedIds.begin(), selectedIds.end());
    std::sort(sortedIds.begin(), sortedIds.end());

    cvt::log::info() << "Selected particle IDs:";
    for (uint32_t particleId : sortedIds)
    {
        cvt::log::info() << ' ' << displayParticleId(particleId);
    }
    cvt::log::info() << std::endl;

    if (sortedIds.size() != 2u)
    {
        return;
    }

    const Particle *firstParticle = findParticleById(particleSystem, sortedIds[0]);
    const Particle *secondParticle = findParticleById(particleSystem, sortedIds[1]);
    if (firstParticle == nullptr || secondParticle == nullptr)
    {
        cvt::log::error() << "Could not resolve both selected particle IDs in the current frame."
                          << std::endl;
        return;
    }

    bx::Vec3 displacement = {
        secondParticle->position.x - firstParticle->position.x,
        secondParticle->position.y - firstParticle->position.y,
        secondParticle->position.z - firstParticle->position.z,
    };
    displacement = simulationBox.nearestImage(displacement);

    const float centerDistance = bx::length(displacement);
    const float radiusSum = particleRadius(*firstParticle) + particleRadius(*secondParticle);
    cvt::log::info() << std::fixed << std::setprecision(6)
                     << "Center distance(" << displayParticleId(sortedIds[0]) << ", "
                     << displayParticleId(sortedIds[1])
                     << "): " << centerDistance << '\n'
                     << "Radius sum(" << displayParticleId(sortedIds[0]) << ", "
                     << displayParticleId(sortedIds[1])
                     << "): " << radiusSum << std::endl;
}

void removeFrankKasperUnbondedVisibilityFilter(ViewerState &viewerState)
{
    for (uint32_t particleId : viewerState.frankKasperAutoHiddenIds)
    {
        viewerState.hiddenIds.erase(particleId);
    }
    viewerState.frankKasperAutoHiddenIds.clear();
}

void removeTwelveCoordinatedAntiparallelVisibilityFilter(ViewerState &viewerState)
{
    for (uint32_t particleId : viewerState.twelveCoordinatedAntiparallelAutoHiddenIds)
    {
        viewerState.hiddenIds.erase(particleId);
    }
    viewerState.twelveCoordinatedAntiparallelAutoHiddenIds.clear();
}

void applyFrankKasperUnbondedVisibilityFilter(ViewerState &viewerState,
                                              const ParticleSystem &particleSystem)
{
    removeFrankKasperUnbondedVisibilityFilter(viewerState);

    if (!viewerState.hideNonFrankKasperUnbonded
        || !particleSystem.hasPatchyMetadata()
        || !particleSystem.hasNeighborAnalysis())
    {
        return;
    }

    const std::vector<Particle> &particles = particleSystem.particles();
    const std::vector<PatchyParticleData> &patchMetadata = particleSystem.patchyMetadata();
    const std::vector<std::vector<NearestNeighborData>> &neighborLists =
        particleSystem.neighborAnalysis();

    for (size_t index = 0u;
         index < particles.size() && index < patchMetadata.size()
             && index < neighborLists.size();
         ++index)
    {
        const size_t fkBondCount = patchMetadata[index].bondIds.size();
        const size_t neighborCount = neighborLists[index].size();
        if (fkBondCount == 0u && neighborCount != kFrankKasperNeighborCountNoHide)
        {
            const uint32_t particleId = particles[index].id;
            if (!viewerState.hiddenIds.contains(particleId))
            {
                viewerState.hiddenIds.insert(particleId);
                viewerState.frankKasperAutoHiddenIds.insert(particleId);
            }
        }
    }
}

void applyTwelveCoordinatedAntiparallelVisibilityFilter(ViewerState &viewerState,
                                                        const ParticleSystem &particleSystem,
                                                        const SimulationBox &simulationBox)
{
    removeTwelveCoordinatedAntiparallelVisibilityFilter(viewerState);

    if (!viewerState.hideNonTwelveCoordinatedAntiparallel
        || !viewerState.twelveCoordinatedBondViewModeEnabled
        || !particleSystem.hasPatchyMetadata())
    {
        return;
    }

    const std::vector<Particle> &particles = particleSystem.particles();
    const std::vector<PatchyParticleData> &patchMetadata = particleSystem.patchyMetadata();

    for (size_t particleIndex = 0u;
         particleIndex < particles.size() && particleIndex < patchMetadata.size();
         ++particleIndex)
    {
        const PatchyParticleData &patchData = patchMetadata[particleIndex];
        if (patchData.bondIds.size() < 2u)
        {
            const uint32_t particleId = particles[particleIndex].id;
            if (!viewerState.hiddenIds.contains(particleId))
            {
                viewerState.hiddenIds.insert(particleId);
                viewerState.twelveCoordinatedAntiparallelAutoHiddenIds.insert(particleId);
            }
            continue;
        }

        std::vector<bx::Vec3> bondDirections;
        bondDirections.reserve(patchData.bondIds.size());
        for (const int32_t bondId : patchData.bondIds)
        {
            if (bondId < 0)
            {
                continue;
            }

            const size_t neighborIndex = static_cast<size_t>(bondId);
            if (neighborIndex >= particles.size())
            {
                continue;
            }

            bx::Vec3 displacement = {
                particles[neighborIndex].position.x - particles[particleIndex].position.x,
                particles[neighborIndex].position.y - particles[particleIndex].position.y,
                particles[neighborIndex].position.z - particles[particleIndex].position.z,
            };
            displacement = simulationBox.nearestImage(displacement);

            const float length = bx::length(displacement);
            if (length <= 1.0e-6f)
            {
                continue;
            }

            const float inverseLength = 1.0f / length;
            bondDirections.push_back({
                displacement.x * inverseLength,
                displacement.y * inverseLength,
                displacement.z * inverseLength,
            });
        }

        bool hasAntiparallelPair = false;
        for (size_t firstIndex = 0u; firstIndex < bondDirections.size(); ++firstIndex)
        {
            for (size_t secondIndex = firstIndex + 1u;
                 secondIndex < bondDirections.size();
                 ++secondIndex)
            {
                if (bx::dot(bondDirections[firstIndex], bondDirections[secondIndex]) < -0.9f)
                {
                    hasAntiparallelPair = true;
                    break;
                }
            }

            if (hasAntiparallelPair)
            {
                break;
            }
        }

        if (!hasAntiparallelPair)
        {
            const uint32_t particleId = particles[particleIndex].id;
            if (!viewerState.hiddenIds.contains(particleId))
            {
                viewerState.hiddenIds.insert(particleId);
                viewerState.twelveCoordinatedAntiparallelAutoHiddenIds.insert(particleId);
            }
        }
    }
}

void resetFrankKasperState(ViewerState &viewerState, bool resetActivationState)
{
    removeFrankKasperUnbondedVisibilityFilter(viewerState);
    removeTwelveCoordinatedAntiparallelVisibilityFilter(viewerState);
    viewerState.frankKasperBondsCached = false;
    viewerState.frankKasperViewModeEnabled = false;
    viewerState.pendingToggleFrankKasperUnbondedVisibility = false;
    viewerState.pendingToggleTwelveCoordinatedAntiparallelVisibility = false;
    viewerState.pendingRecalculateFrankKasperBonds = false;
    if (resetActivationState)
    {
        viewerState.hideNonFrankKasperUnbonded = true;
        viewerState.hideNonTwelveCoordinatedAntiparallel = false;
        viewerState.frankKasperAutoRecalculate = true;
        viewerState.frankKasperViewActivatedOnce = false;
    }
}

bool applyFrankKasperVisibilityModeIfActive(ViewerState &viewerState,
                                            const ParticleSystem &particleSystem)
{
    if (!viewerState.frankKasperViewModeEnabled)
    {
        return false;
    }

    removeTwelveCoordinatedAntiparallelVisibilityFilter(viewerState);

    if (viewerState.hideNonFrankKasperUnbonded)
    {
        applyFrankKasperUnbondedVisibilityFilter(viewerState, particleSystem);
    }
    else
    {
        removeFrankKasperUnbondedVisibilityFilter(viewerState);
    }

    return true;
}

bool applyTwelveCoordinatedVisibilityModeIfActive(ViewerState &viewerState,
                                                  const ParticleSystem &particleSystem,
                                                  const SimulationBox &simulationBox)
{
    if (!viewerState.twelveCoordinatedBondViewModeEnabled)
    {
        return false;
    }

    removeFrankKasperUnbondedVisibilityFilter(viewerState);

    if (viewerState.hideNonTwelveCoordinatedAntiparallel)
    {
        applyTwelveCoordinatedAntiparallelVisibilityFilter(viewerState,
                                                           particleSystem,
                                                           simulationBox);
    }
    else
    {
        removeTwelveCoordinatedAntiparallelVisibilityFilter(viewerState);
    }

    return true;
}

void processAnalysisAndFrankKasperPendingActions(ViewerState &viewerState,
                                                 ParticleSystem &particleSystem,
                                                 const SimulationBox &simulationBox)
{
    if (viewerState.pendingFindNeighbors)
    {
        const bool wasFrankKasperViewModeEnabled = viewerState.frankKasperViewModeEnabled;
        const bool wasTwelveCoordinatedBondViewModeEnabled =
            viewerState.twelveCoordinatedBondViewModeEnabled;
        resetFrankKasperState(viewerState, false);
        viewerState.twelveCoordinatedBondViewModeEnabled = false;
        findNearestNeighbors(viewerState, simulationBox, particleSystem);
        viewerState.neighborAnalysisValid = particleSystem.hasNeighborAnalysis();
        markNearestNeighborRenderSystemsDirty(viewerState);
        markBondDiagramGeometryDirty(viewerState);
        if (viewerState.neighborAnalysisValid)
        {
            computeAnalysisResults(viewerState, particleSystem);
            if (viewerState.analysisColorMode != AnalysisColorMode::Disabled)
            {
                markColorDependentHelperSystemsDirty(viewerState);
            }
        }
        else
        {
            particleSystem.clearAnalysisResults();
            if (viewerState.analysisColorMode != AnalysisColorMode::Disabled)
            {
                markColorDependentHelperSystemsDirty(viewerState);
            }
        }
        viewerState.pendingFindNeighbors = false;
        viewerState.pendingRefreshAnalysisResults = false;
        if (viewerState.neighborAnalysisValid
            && viewerState.frankKasperAutoRecalculate
            && viewerState.frankKasperViewActivatedOnce)
        {
            viewerState.frankKasperViewModeEnabled = wasFrankKasperViewModeEnabled;
            viewerState.pendingRecalculateFrankKasperBonds = true;
        }
        if (viewerState.neighborAnalysisValid && wasTwelveCoordinatedBondViewModeEnabled)
        {
            viewerState.pendingActivateTwelveCoordinatedBondView = true;
        }
        const bool visibilityChanged = applyParticleVisibilityFilters(particleSystem,
                                                                      viewerState);
        if (visibilityChanged)
        {
            markVisibilityDependentHelperSystemsDirty(viewerState);
        }
        markPickBufferDirty(viewerState);
    }

    if (viewerState.pendingRefreshAnalysisResults)
    {
        if (viewerState.neighborAnalysisValid)
        {
            computeAnalysisResults(viewerState, particleSystem);
            if (viewerState.analysisColorMode != AnalysisColorMode::Disabled)
            {
                markColorDependentHelperSystemsDirty(viewerState);
            }
            markPickBufferDirty(viewerState);
        }
        else
        {
            particleSystem.clearAnalysisResults();
            if (viewerState.analysisColorMode != AnalysisColorMode::Disabled)
            {
                markColorDependentHelperSystemsDirty(viewerState);
            }
        }
        viewerState.pendingRefreshAnalysisResults = false;
        markPickBufferDirty(viewerState);
    }

    if (viewerState.pendingToggleFrankKasperUnbondedVisibility)
    {
        if (viewerState.frankKasperViewActivatedOnce)
        {
            if (applyFrankKasperVisibilityModeIfActive(viewerState, particleSystem))
            {
                const bool visibilityChanged = applyParticleVisibilityFilters(particleSystem,
                                                                              viewerState);
                if (visibilityChanged)
                {
                    markVisibilityDependentHelperSystemsDirty(viewerState);
                }
                markPickBufferDirty(viewerState);
            }
        }
        viewerState.pendingToggleFrankKasperUnbondedVisibility = false;
    }

    if (viewerState.pendingToggleTwelveCoordinatedAntiparallelVisibility)
    {
        if (applyTwelveCoordinatedVisibilityModeIfActive(viewerState,
                                                         particleSystem,
                                                         simulationBox))
        {
            const bool visibilityChanged = applyParticleVisibilityFilters(particleSystem,
                                                                          viewerState);
            if (visibilityChanged)
            {
                markVisibilityDependentHelperSystemsDirty(viewerState);
            }
            markPickBufferDirty(viewerState);
        }
        viewerState.pendingToggleTwelveCoordinatedAntiparallelVisibility = false;
    }

    if (viewerState.pendingRecalculateFrankKasperBonds)
    {
        if (!viewerState.neighborAnalysisValid)
        {
            viewerState.pendingFindNeighbors = true;
        }
        else
        {
            calculateFrankKasperBonds(particleSystem, particleSystem);
            viewerState.frankKasperBondsCached = true;

            if (applyFrankKasperVisibilityModeIfActive(viewerState, particleSystem))
            {
                const bool visibilityChanged = applyParticleVisibilityFilters(particleSystem,
                                                                              viewerState);
                if (visibilityChanged)
                {
                    markVisibilityDependentHelperSystemsDirty(viewerState);
                }

                markColorDependentHelperSystemsDirty(viewerState);
            }

            markBondLikeHelperSystemsDirty(viewerState);
            markPickBufferDirty(viewerState);
            viewerState.pendingRecalculateFrankKasperBonds = false;
        }
    }

    if (viewerState.pendingActivateFrankKasperView)
    {
        if (!viewerState.neighborAnalysisValid)
        {
            viewerState.pendingFindNeighbors = true;
        }
        else
        {
            if (!viewerState.frankKasperBondsCached)
            {
                calculateFrankKasperBonds(particleSystem, particleSystem);
                viewerState.frankKasperBondsCached = true;
            }

            viewerState.bondModeEnabled = true;
            viewerState.mobilityModeEnabled = false;
            viewerState.nearestNeighborModeEnabled = false;
            viewerState.colorMode = ColorMode::BondCount;
            viewerState.analysisColorMode = AnalysisColorMode::Disabled;
            viewerState.frankKasperViewModeEnabled = true;
            viewerState.twelveCoordinatedBondViewModeEnabled = false;
            viewerState.frankKasperViewActivatedOnce = true;
            viewerState.frankKasperAutoRecalculate = true;

            applyFrankKasperVisibilityModeIfActive(viewerState, particleSystem);

            const bool visibilityChanged =
                applyParticleVisibilityFilters(particleSystem, viewerState);
            if (visibilityChanged)
            {
                markVisibilityDependentHelperSystemsDirty(viewerState);
            }

            viewerState.pendingActivateFrankKasperView = false;

            markColorDependentHelperSystemsDirty(viewerState);
            markBondLikeHelperSystemsDirty(viewerState);
            markPickBufferDirty(viewerState);
        }
    }

    if (viewerState.pendingActivateTwelveCoordinatedBondView)
    {
        if (!viewerState.neighborAnalysisValid)
        {
            viewerState.pendingFindNeighbors = true;
        }
        else
        {
            calculateTwelveCoordinatedNeighborBonds(particleSystem, particleSystem);

            viewerState.bondModeEnabled = true;
            viewerState.mobilityModeEnabled = false;
            viewerState.nearestNeighborModeEnabled = false;
            viewerState.colorMode = ColorMode::BondCount;
            viewerState.analysisColorMode = AnalysisColorMode::Disabled;
            viewerState.twelveCoordinatedBondViewModeEnabled = true;
            viewerState.frankKasperViewModeEnabled = false;

            removeFrankKasperUnbondedVisibilityFilter(viewerState);
            applyTwelveCoordinatedVisibilityModeIfActive(viewerState,
                                                         particleSystem,
                                                         simulationBox);

            const bool visibilityChanged =
                applyParticleVisibilityFilters(particleSystem, viewerState);
            if (visibilityChanged)
            {
                markVisibilityDependentHelperSystemsDirty(viewerState);
            }

            viewerState.pendingActivateTwelveCoordinatedBondView = false;

            markColorDependentHelperSystemsDirty(viewerState);
            markBondLikeHelperSystemsDirty(viewerState);
            markPickBufferDirty(viewerState);
        }
    }
}

void processPendingFileOpenAction(ViewerState &viewerState,
                                  ParticleSystem &particleSystem,
                                  const bgfx::VertexLayout &layout,
                                  TrajectoryReader::FileType &particleFileType,
                                  SimulationBox &simulationBox,
                                  std::unique_ptr<TrajectoryReader> &trajectoryReader,
                                  StructureFactorResources &structureFactorResources,
                                  std::string &loadedPath,
                                  size_t &currentFrame,
                                  size_t &totalFrames,
                                  uint16_t sphereStacks,
                                  uint16_t sphereSlices)
{
    if (!viewerState.pendingOpenDroppedFile)
    {
        return;
    }

    if (!viewerState.pendingOpenPath.empty())
    {
        openTrajectoryFile(viewerState.pendingOpenPath, viewerState, layout,
                           sphereStacks, sphereSlices, trajectoryReader,
                           particleSystem, particleFileType, simulationBox,
                           structureFactorResources, loadedPath,
                           currentFrame, totalFrames);
    }
    viewerState.pendingOpenPath.clear();
    viewerState.pendingOpenDroppedFile = false;
}

void processSelectionAndVisibilityPendingActions(ViewerState &viewerState,
                                                 ParticleSystem &particleSystem,
                                                 const SimulationBox &simulationBox,
                                                 TrajectoryReader::FileType particleFileType,
                                                 float particleSizeScale)
{
    if (viewerState.pendingHideSelected)
    {
        const bool hiddenChanged = hideSelectedParticles(particleSystem,
                                                         viewerState.selectedIds,
                                                         viewerState.hiddenIds);
        const bool visibilityChanged = applyParticleVisibilityFilters(particleSystem,
                                                                      viewerState);
        if (hiddenChanged || visibilityChanged)
        {
            markVisibilityDependentHelperSystemsDirty(viewerState);
            markPickBufferDirty(viewerState);
        }
        viewerState.pendingHideSelected = false;
        viewerState.lastPickedId = 0;
        viewerState.pendingPickRequest = false;
    }

    if (viewerState.pendingRevealAll)
    {
        const bool hiddenChanged = revealAllParticles(particleSystem, viewerState.hiddenIds);
        viewerState.frankKasperAutoHiddenIds.clear();
        const bool visibilityChanged = applyParticleVisibilityFilters(particleSystem,
                                                                      viewerState);
        if (hiddenChanged || visibilityChanged)
        {
            markVisibilityDependentHelperSystemsDirty(viewerState);
            markPickBufferDirty(viewerState);
        }
        viewerState.pendingRevealAll = false;
        viewerState.pendingPickRequest = false;
    }

    if (viewerState.pendingOverlapCheck)
    {
        if (isSphereLikeFileType(particleFileType))
        {
            viewerState.selectedIds.clear();
            findOverlappingSphereParticles(particleSystem, simulationBox,
                                           particleSizeScale,
                                           viewerState.selectedIds);
            markPickBufferDirty(viewerState);
        }
        viewerState.pendingOverlapCheck = false;
    }

    if (viewerState.pendingApplyParticleTypeVisibility)
    {
        applyParticleVisibilityFilters(particleSystem, viewerState);
        markVisibilityDependentHelperSystemsDirty(viewerState);
        markPickBufferDirty(viewerState);
        viewerState.pendingApplyParticleTypeVisibility = false;
    }

    if (viewerState.pendingInvertSelected)
    {
        invertSelection(particleSystem, viewerState.selectedIds);
        viewerState.pendingInvertSelected = false;
    }

    if (viewerState.pendingSelectBonded)
    {
        selectBondedNeighbors(particleSystem, viewerState.selectedIds);
        viewerState.pendingSelectBonded = false;
        markPickBufferDirty(viewerState);
    }

    if (viewerState.pendingSelectNearestNeighbors)
    {
        if (viewerState.neighborAnalysisValid && particleSystem.hasNeighborAnalysis())
        {
            selectNearestNeighbors(particleSystem, viewerState.selectedIds);
            viewerState.pendingSelectNearestNeighbors = false;
            markPickBufferDirty(viewerState);
        }
        else
        {
            viewerState.pendingOpenNeighborAnalysisPanel = true;
            viewerState.pendingFindNeighbors = true;
        }
    }
}

} // namespace

static ViewerState *getViewerState(GLFWwindow *window)
{
    return static_cast<ViewerState *>(glfwGetWindowUserPointer(window));
}

static void glfw_mouseCallback(GLFWwindow *window, double xpos, double ypos)
{
    ViewerState *state = getViewerState(window);
    if (state == nullptr)
    {
        return;
    }

    state->mouseX = xpos;
    state->mouseY = ypos;
}

static void glfw_errorCallback(int error, const char *description)
{
    cvt::log::errorf("GLFW error %d: %s\n", error, description);
}

static void glfw_charCallback(GLFWwindow *window, unsigned int codePoint)
{
    ViewerState *state = getViewerState(window);
    if (state == nullptr)
    {
        return;
    }

    ImGuiBgfx::addInputCharacter(codePoint);
}

static void glfw_dropCallback(GLFWwindow *window, int pathCount, const char **paths)
{
    ViewerState *state = getViewerState(window);
    if (state == nullptr || pathCount <= 0 || paths == nullptr || paths[0] == nullptr)
    {
        return;
    }

    state->pendingOpenPath = paths[0];
    state->pendingOpenDroppedFile = true;
}

static void glfw_keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    ViewerState *state = getViewerState(window);
    if (state == nullptr)
    {
        return;
    }

    if (action == GLFW_RELEASE)
    {
        return;
    }

    const int particleTypeHotkey = particleTypeHotkeyIndex(key);
    if (action == GLFW_PRESS
        && particleTypeHotkey >= 0
        && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER)) == 0)
    {
        if ((mods & GLFW_MOD_SHIFT) != 0)
        {
            state->particleTypeVisible.fill(false);
            state->particleTypeVisible[particleTypeHotkey] = true;
        }
        else
        {
            state->particleTypeVisible[particleTypeHotkey] =
                !state->particleTypeVisible[particleTypeHotkey];
        }
        state->pendingApplyParticleTypeVisibility = true;
        return;
    }

    switch(key)
    {
        case GLFW_KEY_F1:
            if(action == GLFW_PRESS) state->showStats = !state->showStats;
            break;
        case GLFW_KEY_F2:
            if(action == GLFW_PRESS) state->showUi = !state->showUi;
            break;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:
            bx::mtxIdentity(state->sceneRotation);
            state->structureFactorInteractionLowResActive = false;
            markBondDiagramViewDirty(*state);
            markStructureFactorDirty(*state);
            if (state->structureFactorPanelOpen
                && structureFactorAllowsAutomaticUpdates(state->structureFactorUpdateMode))
            {
                state->structureFactorPendingCompute = true;
            }
            markPickBufferDirty(*state);
            break;
        case GLFW_KEY_LEFT:
            if ((mods & GLFW_MOD_CONTROL) != 0)
            {
                applySceneRotation(*state, 0.0f, 0.5f * bx::kPi, 0.0f);
                if (state->structureFactorPanelOpen
                    && structureFactorAllowsAutomaticUpdates(state->structureFactorUpdateMode))
                {
                    state->structureFactorPendingCompute = true;
                }
                markPickBufferDirty(*state);
            }
            else if ((mods & GLFW_MOD_SHIFT) != 0)
            {
                state->jumpToFirstFrame = true;
            }
            else
            {
                state->pendingFrameStep = -1;
            }        
            break;
        case GLFW_KEY_RIGHT:
            if ((mods & GLFW_MOD_CONTROL) != 0)
            {
                applySceneRotation(*state, 0.0f, -0.5f * bx::kPi, 0.0f);
                if (state->structureFactorPanelOpen
                    && structureFactorAllowsAutomaticUpdates(state->structureFactorUpdateMode))
                {
                    state->structureFactorPendingCompute = true;
                }
                markPickBufferDirty(*state);
            }        
            else if ((mods & GLFW_MOD_SHIFT) != 0)
            {
                state->jumpToLastFrame = true;
            }
            else
            {
                state->pendingFrameStep = 1;
            }        
            break;
        case GLFW_KEY_UP:
            if ((mods & GLFW_MOD_CONTROL) != 0)
            {
                applySceneRotation(*state, 0.5f * bx::kPi, 0.0f, 0.0f);
                if (state->structureFactorPanelOpen
                    && structureFactorAllowsAutomaticUpdates(state->structureFactorUpdateMode))
                {
                    state->structureFactorPendingCompute = true;
                }
                markPickBufferDirty(*state);
            }
            break;
        case GLFW_KEY_DOWN:
            if ((mods & GLFW_MOD_CONTROL) != 0)
            {
                applySceneRotation(*state, -0.5f * bx::kPi, 0.0f, 0.0f);
                if (state->structureFactorPanelOpen
                    && structureFactorAllowsAutomaticUpdates(state->structureFactorUpdateMode))
                {
                    state->structureFactorPendingCompute = true;
                }
                markPickBufferDirty(*state);
            }
            break;
        case GLFW_KEY_ESCAPE:
        case GLFW_KEY_Q:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            break;
        case GLFW_KEY_P:
            if (action == GLFW_PRESS)
            {
                state->pendingScreenshotRequest = true;
            }
            break;
        case GLFW_KEY_SLASH:
        case GLFW_KEY_KP_DIVIDE:
            if (action == GLFW_PRESS || action == GLFW_REPEAT)
            {
                state->particleSizeScale =
                    bx::max(kMinParticleSizeScale,
                            state->particleSizeScale - kParticleSizeScaleStep);
                markPickBufferDirty(*state);
            }
            break;
        case GLFW_KEY_8:
            if (action == GLFW_PRESS && (mods & GLFW_MOD_SHIFT) != 0)
            {
                state->particleSizeScale += kParticleSizeScaleStep;
                markPickBufferDirty(*state);
            }
            break;
        case GLFW_KEY_KP_MULTIPLY:
            if (action == GLFW_PRESS  || action == GLFW_REPEAT)
            {
                state->particleSizeScale += kParticleSizeScaleStep;
                markPickBufferDirty(*state);
            }
            break;
        case GLFW_KEY_W:
            {
                if(action == GLFW_PRESS && !(mods & GLFW_MOD_SHIFT))
                {
                    state->wrapParticlesToBox = !state->wrapParticlesToBox;
                    markBondLikeHelperSystemsDirty(*state);
                    markPickBufferDirty(*state);
                }
            }       
            break;
        case GLFW_KEY_H:
            if (action == GLFW_PRESS)
            {
                if ((mods & GLFW_MOD_SHIFT) != 0)
                {
                    state->pendingRevealAll = true;
                }
                else
                {
                    state->pendingHideSelected = true;
                }
            }
            break;
        case GLFW_KEY_B:
            if (action == GLFW_PRESS)
            {
                if ((mods & GLFW_MOD_CONTROL) != 0)
                {
                    state->pendingSelectBonded = true;
                }
                else if ((mods & GLFW_MOD_SHIFT) != 0)
                {
                    state->bondModeEnabled = !state->bondModeEnabled;
                    if (state->bondModeEnabled)
                    {
                        state->mobilityModeEnabled = false;
                        state->nearestNeighborModeEnabled = false;
                    }
                    markPickBufferDirty(*state);
                }
                else
                {
                    state->showBox = !state->showBox;
                }
            }
            break;
        case GLFW_KEY_A:
            if (action == GLFW_PRESS
                && (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER)) == 0)
            {
                state->pendingAlignViewToSelection = true;
            }
            break;
        case GLFW_KEY_D:
            if (action == GLFW_PRESS)
            {
                state->pendingDescribeSelection = true;
            }
            break;
        case GLFW_KEY_V:
            if (action == GLFW_PRESS)
            {
                state->pendingDescribeVisibleCount = true;
            }
            break;
        case GLFW_KEY_E:
            if (action == GLFW_PRESS)
            {
                state->pendingOverlapCheck = true;
            }
            break;
        case GLFW_KEY_M:
            if (action == GLFW_PRESS && (mods & GLFW_MOD_SHIFT) != 0)
            {
                state->mobilityModeEnabled = !state->mobilityModeEnabled;
                if (state->mobilityModeEnabled)
                {
                    state->bondModeEnabled = false;
                    state->nearestNeighborModeEnabled = false;
                }
                markPickBufferDirty(*state);
            }
            else if (action == GLFW_PRESS)
            {
                clampColorModeToAvailable(*state);
                const size_t nextMode =
                    (static_cast<size_t>(state->colorMode) + 1u)
                    % availableColorModeCount(*state);
                state->colorMode = static_cast<ColorMode>(nextMode);
                state->frankKasperViewModeEnabled = false;
                markColorDependentHelperSystemsDirty(*state);
            }
            break;
        case GLFW_KEY_N:
            if (action == GLFW_PRESS && (mods & GLFW_MOD_SHIFT) != 0)
            {
                state->nearestNeighborModeEnabled = !state->nearestNeighborModeEnabled;
                if (state->nearestNeighborModeEnabled)
                {
                    state->bondModeEnabled = false;
                    state->mobilityModeEnabled = false;
                }
                markPickBufferDirty(*state);
            }
            else if (action == GLFW_PRESS && (mods & GLFW_MOD_CONTROL) != 0)
            {
                state->pendingSelectNearestNeighbors = true;
            }
            break;
        case GLFW_KEY_S:
            if (action == GLFW_PRESS)
            {
                state->lightingLevelIndex =
                    static_cast<uint8_t>((state->lightingLevelIndex + 1u) % kLightingLevelCount);
                markBondDiagramViewDirty(*state);
            }
            break;
        case GLFW_KEY_I:
            if (action == GLFW_PRESS)
            {
                if ((mods & GLFW_MOD_SHIFT) == 0)
                {
                    state->pendingInvertSelected = true;
                }
            }
            break;
        case GLFW_KEY_U:
            if (action == GLFW_PRESS)
            {
                if ((mods & GLFW_MOD_SHIFT) != 0)
                {
                    state->showUi = !state->showUi;
                }
                else
                {
                    state->pendingUnselect = true;
                }
            }
            break;
        case GLFW_KEY_EQUAL:
            if (action == GLFW_PRESS && (mods & GLFW_MOD_SHIFT) != 0)
            {
                state->pendingIncreaseSphereResolution = true;
            }
            break;
        case GLFW_KEY_KP_ADD:
            if (action == GLFW_PRESS)
            {
                state->pendingIncreaseSphereResolution = true;
            }
            break;
        case GLFW_KEY_PERIOD:
            if (action == GLFW_PRESS || action == GLFW_REPEAT)
            {
                if ((mods & (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT))
                    == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT))
                {
                    state->pendingEnableSphericalCut = true;
                }
                else if ((mods & GLFW_MOD_CONTROL) != 0)
                {
                    --state->pendingSphericalCutStep;
                }
                else if ((mods & GLFW_MOD_SHIFT) != 0)
                {
                    state->pendingEnableCutPlane = true;
                }
                else
                {
                    --state->pendingCutPlaneStep;
                }
            }
            break;
        case GLFW_KEY_COMMA:
            if (action == GLFW_PRESS || action == GLFW_REPEAT)
            {
                if ((mods & (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT))
                    == (GLFW_MOD_CONTROL | GLFW_MOD_SHIFT))
                {
                    state->pendingDisableSphericalCut = true;
                }
                else if ((mods & GLFW_MOD_CONTROL) != 0)
                {
                    ++state->pendingSphericalCutStep;
                }
                else if ((mods & GLFW_MOD_SHIFT) != 0)
                {
                    state->pendingDisableCutPlane = true;
                }
                else
                {
                    ++state->pendingCutPlaneStep;
                }
            }
            break;
        case GLFW_KEY_MINUS:
        case GLFW_KEY_KP_SUBTRACT:
            if (action == GLFW_PRESS)
            {
                state->pendingDecreaseSphereResolution = true;
            }
            break;
        
                        
    }
    
}

static void glfw_mouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
{
    ViewerState *state = getViewerState(window);
    if (state == nullptr)
    {
        return;
    }

    if (action == GLFW_PRESS || action == GLFW_RELEASE)
    {
        ImGuiBgfx::addMouseButtonEvent(button, action == GLFW_PRESS);
    }

    const bool wantsMouseCapture = ImGuiBgfx::wantsMouseCapture();
    const bool inRenderViewport = isInRenderViewport(*state, state->mouseX, state->mouseY);

    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        if (action == GLFW_PRESS)
        {
            if (wantsMouseCapture || !inRenderViewport)
            {
                return;
            }
            state->leftMouseDown = true;
            state->leftDragActive = false;
            state->leftTranslateMode = (mods & GLFW_MOD_SHIFT) != 0;
            state->leftMousePressX = state->mouseX;
            state->leftMousePressY = state->mouseY;
            state->lastMouseX = state->mouseX;
            state->lastMouseY = state->mouseY;
        }
        else if (action == GLFW_RELEASE)
        {
            if (!wantsMouseCapture && inRenderViewport
                && !state->leftDragActive && !state->leftTranslateMode)
            {
                state->pendingPickRequest = true;
                state->pendingPickX = clampPickCoordinate(state->mouseX,
                                                          state->renderViewportWidth);
                state->pendingPickY = clampPickCoordinate(state->mouseY,
                                                          state->renderViewportHeight);
            }

            const bool hadLowResInteractionRender = state->structureFactorInteractionLowResActive;
            const bool hadRdfLowResInteractionRender = state->rdfInteractionLowResActive;
            state->leftMouseDown = false;
            state->leftDragActive = false;
            state->leftTranslateMode = false;
            if (hadLowResInteractionRender)
            {
                state->structureFactorInteractionLowResActive = false;
                if (state->structureFactorPanelOpen
                    && structureFactorAllowsAutomaticUpdates(state->structureFactorUpdateMode)
                    && state->structureFactorDirty)
                {
                    state->structureFactorPendingCompute = true;
                }
            }
            else if (state->structureFactorPanelOpen
                     && state->structureFactorDirty
                     && state->rightMouseDown == false
                     && state->leftMouseDown == false
                     && state->leftDragActive == false
                     && state->leftTranslateMode == false
                     && state->structureFactorUpdateMode
                            == StructureFactorUpdateMode::UpdateWhenStationary)
            {
                state->structureFactorPendingCompute = true;
            }

            if (hadRdfLowResInteractionRender)
            {
                state->rdfInteractionLowResActive = false;
                if (state->rdfPanelOpen && state->rdfAuto && state->rdfNeedsFullResolutionRefine)
                {
                    markRdfDirty(*state);
                    state->rdfNeedsFullResolutionRefine = false;
                    state->rdfPendingCompute = true;
                }
            }
        }
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        if (action == GLFW_PRESS)
        {
            if (wantsMouseCapture || !inRenderViewport)
            {
                return;
            }
            state->rightMouseDown = true;
            state->lastMouseX = state->mouseX;
            state->lastMouseY = state->mouseY;
        }
        else if (action == GLFW_RELEASE)
        {
            const bool hadLowResInteractionRender = state->structureFactorInteractionLowResActive;
            const bool hadRdfLowResInteractionRender = state->rdfInteractionLowResActive;
            state->rightMouseDown = false;
            if (hadLowResInteractionRender)
            {
                state->structureFactorInteractionLowResActive = false;
                if (state->structureFactorPanelOpen
                    && structureFactorAllowsAutomaticUpdates(state->structureFactorUpdateMode)
                    && state->structureFactorDirty)
                {
                    state->structureFactorPendingCompute = true;
                }
            }
            else if (state->structureFactorPanelOpen
                     && state->structureFactorDirty
                     && state->rightMouseDown == false
                     && state->leftMouseDown == false
                     && state->leftDragActive == false
                     && state->leftTranslateMode == false
                     && state->structureFactorUpdateMode
                            == StructureFactorUpdateMode::UpdateWhenStationary)
            {
                state->structureFactorPendingCompute = true;
            }

            if (hadRdfLowResInteractionRender)
            {
                state->rdfInteractionLowResActive = false;
                if (state->rdfPanelOpen && state->rdfAuto && state->rdfNeedsFullResolutionRefine)
                {
                    markRdfDirty(*state);
                    state->rdfNeedsFullResolutionRefine = false;
                    state->rdfPendingCompute = true;
                }
            }
        }
    }
}

static void glfw_scrollCallback(GLFWwindow *window, double xoffset, double yoffset)
{
    ViewerState *state = getViewerState(window);
    s_uiScrollX += static_cast<float>(xoffset);
    s_uiScrollY += static_cast<float>(yoffset);
    if (state == nullptr || yoffset == 0.0 || ImGuiBgfx::wantsMouseCapture())
    {
        return;
    }

    const int mods = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                             || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS
                         ? GLFW_MOD_CONTROL
                         : 0;

    if ((mods & GLFW_MOD_CONTROL) != 0)
    {
        if (state->cutPlaneEnabled)
        {
            state->pendingCutPlaneStep -= static_cast<int>(std::lround(yoffset));
            markPickBufferDirty(*state);
        }
        return;
    }

    float zoomScale = std::pow(kZoomPerWheelStep, static_cast<float>(yoffset));
    state->orthoZoom = std::clamp(state->orthoZoom * zoomScale, kMinOrthoZoom, kMaxOrthoZoom);
    markPickBufferDirty(*state);
}

static bool hasValidParticleMeshes(const ParticleSystem &particleSystem)
{
    const std::vector<RenderPart> &parts = particleSystem.type().renderParts();
    if (parts.empty())
    {
        return false;
    }

    for (const RenderPart &part : parts)
    {
        if (part.mesh == nullptr || !part.mesh->isValid())
        {
            return false;
        }
    }

    return true;
}

static void handleWindowResize(GLFWwindow *window, int &width, int &height,
                               ViewerState &viewerState, PickResources &pickResources)
{
    const int oldWidth = width;
    const int oldHeight = height;
    const uint16_t oldRenderWidth = viewerState.renderViewportWidth;
    const uint16_t oldRenderHeight = viewerState.renderViewportHeight;
    glfwGetWindowSize(window, &width, &height);
    const uint16_t newRenderWidth = computeRenderViewportWidth(static_cast<uint16_t>(width),
                                                               viewerState.showUi);
    const uint16_t newRenderHeight = static_cast<uint16_t>(bx::max(height, 1));
    viewerState.uiPanelWidth = computeUiPanelWidth(static_cast<uint16_t>(width), viewerState.showUi);
    viewerState.renderViewportWidth = newRenderWidth;
    viewerState.renderViewportHeight = newRenderHeight;
    if (width == oldWidth && height == oldHeight
        && newRenderWidth == oldRenderWidth && newRenderHeight == oldRenderHeight)
    {
        return;
    }

    if (width != oldWidth || height != oldHeight)
    {
        bgfx::reset((uint32_t)width, (uint32_t)height, BGFX_RESET_VSYNC);
    }
    bgfx::setViewRect(kMainView, 0, 0, viewerState.renderViewportWidth,
                      viewerState.renderViewportHeight);
    viewerState.pendingPickReadback = false;
    markPickBufferDirty(viewerState);
    if (!createPickResources(pickResources, viewerState.renderViewportWidth,
                             viewerState.renderViewportHeight))
    {
        cvt::log::error() << "GPU picking disabled after resize: "
                  << pickResources.disableReason << std::endl;
    }
}

static bool openTrajectoryFile(const std::string &path,
                             ViewerState &viewerState,
                             const bgfx::VertexLayout &layout,
                             uint16_t sphereStacks, uint16_t sphereSlices,
                             std::unique_ptr<TrajectoryReader> &trajectoryReader,
                             ParticleSystem &particleSystem,
                             TrajectoryReader::FileType &particleFileType,
                             SimulationBox &simulationBox,
                             StructureFactorResources &structureFactorResources,
                             std::string &loadedPath,
                             size_t &currentFrame,
                             size_t &totalFrames)
{
    std::unique_ptr<TrajectoryReader> openedTrajectory = std::make_unique<TrajectoryReader>(path);
    if (!openedTrajectory->isOpen())
    {
        viewerState.fileOpenStatusMessage = openedTrajectory->error();
        cvt::log::error() << viewerState.fileOpenStatusMessage << std::endl;
        return false;
    }

    std::unique_ptr<ParticleType> loadedParticleType;
    try
    {
        loadedParticleType = createParticleType(layout, openedTrajectory->fileType(),
                                                openedTrajectory->voronoiPointSets(),
                                                sphereStacks, sphereSlices);
    }
    catch (const std::exception &exception)
    {
        viewerState.fileOpenStatusMessage = "Failed to open trajectory file: " + path
                                            + " (" + exception.what() + ")";
        cvt::log::error() << viewerState.fileOpenStatusMessage << std::endl;
        return false;
    }

    ParticleSystem loadedParticleSystem(std::move(loadedParticleType));
    SimulationBox loadedSimulationBox = simulationBox;
    if (!openedTrajectory->loadFrame(0, loadedParticleSystem, loadedSimulationBox))
    {
        cvt::log::error() << "Failed to load frame 0 from trajectory file: " << path;
        if (!openedTrajectory->error().empty())
        {
            cvt::log::error() << "\n" << openedTrajectory->error();
        }
        cvt::log::error() << std::endl;
        viewerState.fileOpenStatusMessage = "Failed to open trajectory file: " + path;
        return false;
    }

    trajectoryReader = std::move(openedTrajectory);
    particleSystem = std::move(loadedParticleSystem);
    simulationBox = loadedSimulationBox;
    loadedPath = path;
    currentFrame = 0;
    totalFrames = bx::max<size_t>(trajectoryReader->frameCount(), 1u);
    particleFileType = trajectoryReader->fileType();
    viewerState.fileDimensionality = trajectoryReader->dimensionality();
    viewerState.maxSeenParticleTypeIndex = 0u;
    viewerState.orderParameterCount = 0u;
    viewerState.particleTypeVisible.fill(true);
    viewerState.bondOrderScatterTypeEnabled.fill(true);
    viewerState.bondOrderScatterInteraction = {};
    viewerState.bondOrderScatterCache = {};
    viewerState.bondOrderScatterCache.enabledSpecies.fill(true);
    viewerState.particleColorStatsCache = {};
    viewerState.mobilityColorStatsCache = {};
    viewerState.selectedIds.clear();
    viewerState.hiddenIds.clear();
    resetFrankKasperState(viewerState, true);
    viewerState.lastPickedId = 0u;
    viewerState.pendingPickRequest = false;
    viewerState.pendingPickReadback = false;
    viewerState.structureFactorInteractionLowResActive = false;
    noteEncounteredParticleTypes(viewerState, particleSystem);
    invalidateNeighborAnalysis(viewerState, particleSystem);
    applyParticleVisibilityFilters(particleSystem, viewerState);
    markAllHelperSystemsDirty(viewerState);
    markPickBufferDirty(viewerState);
    destroyStructureFactorResources(structureFactorResources);
    markStructureFactorDirty(viewerState);
    viewerState.structureFactorUpdateMode =
        particleSystem.particles().size() <= kLargeStructureFactorParticleThreshold
            ? StructureFactorUpdateMode::UpdateAlways
            : StructureFactorUpdateMode::UpdateWhenStationary;
    viewerState.structureFactorBatchState = {};
    viewerState.structureFactorPendingCompute = false;
    viewerState.rdfDirty = true;
    viewerState.rdfPendingCompute = false;
    viewerState.rdfInteractionLowResActive = false;
    viewerState.rdfNeedsFullResolutionRefine = false;
    viewerState.rdfStatusText.clear();
    viewerState.rdfBinCenters.clear();
    viewerState.rdfValues.clear();
    viewerState.rdfPairCurves.clear();
    viewerState.rdfSampleParticleCount = 0u;
    viewerState.rdfComputedRadius = 0.0f;
    viewerState.rdfBinWidth = 0.0f;
    viewerState.rdfBatchState = {};
    snapshotCurrentParticlePositions(particleSystem, viewerState, false);
    viewerState.fileOpenStatusMessage.clear();
    cvt::log::info() << "Opened trajectory file: " << loadedPath << std::endl;
    return true;
}

static void handleTrajectoryFrameChange(ViewerState &viewerState, size_t &currentFrame,
                                        size_t totalFrames,
                                        const std::string &loadedPath,
                                        TrajectoryReader *trajectoryReader,
                                        ParticleSystem &particleSystem,
                                        SimulationBox &simulationBox)
{
    if (trajectoryReader == nullptr || totalFrames == 0)
    {
        return;
    }

    size_t requestedFrame = currentFrame;

    if (viewerState.jumpToFirstFrame)
    {
        requestedFrame = 0;
    }
    else if (viewerState.jumpToLastFrame)
    {
        requestedFrame = totalFrames - 1;
    }
    else if (viewerState.pendingFrameIndex >= 0)
    {
        requestedFrame = std::min<size_t>(static_cast<size_t>(viewerState.pendingFrameIndex),
                                          totalFrames - 1);
    }
    else if (viewerState.pendingFrameStep < 0 && currentFrame > 0)
    {
        requestedFrame = currentFrame - 1;
    }
    else if (viewerState.pendingFrameStep > 0 && currentFrame + 1 < totalFrames)
    {
        requestedFrame = currentFrame + 1;
    }

    viewerState.pendingFrameStep = 0;
    viewerState.pendingFrameIndex = -1;
    viewerState.jumpToFirstFrame = false;
    viewerState.jumpToLastFrame = false;

    if (requestedFrame == currentFrame)
    {
        return;
    }

    std::vector<bx::Vec3> previousRawPositions;
    previousRawPositions.reserve(particleSystem.particles().size());
    for (const Particle &particle : particleSystem.particles())
    {
        previousRawPositions.push_back(particle.position);
    }

    if (trajectoryReader->loadFrame(requestedFrame, particleSystem, simulationBox))
    {
        currentFrame = requestedFrame;
        viewerState.previousRawPositions = std::move(previousRawPositions);
        viewerState.hasPreviousFramePositions = true;
        viewerState.particleColorStatsCache = {};
        viewerState.mobilityColorStatsCache = {};
        noteEncounteredParticleTypes(viewerState, particleSystem);
        invalidateNeighborAnalysis(viewerState, particleSystem);
        viewerState.pendingFindNeighbors = viewerState.autoFindNeighbors;
        applyParticleVisibilityFilters(particleSystem, viewerState);
        markAllHelperSystemsDirty(viewerState);
        markPickBufferDirty(viewerState);
        viewerState.structureFactorInteractionLowResActive = false;
        viewerState.structureFactorBatchState = {};
        viewerState.structureFactorPendingCompute =
            viewerState.structureFactorPanelOpen
            && structureFactorAllowsAutomaticUpdates(viewerState.structureFactorUpdateMode);
        viewerState.rdfInteractionLowResActive = false;
        viewerState.rdfNeedsFullResolutionRefine = false;
        viewerState.rdfPendingCompute = viewerState.rdfPanelOpen && viewerState.rdfAuto;
        return;
    }

    cvt::log::error() << "Failed to load frame " << requestedFrame
                      << " from trajectory file: " << loadedPath;
    if (trajectoryReader != nullptr && !trajectoryReader->error().empty())
    {
        cvt::log::error() << "\n" << trajectoryReader->error();
    }
    cvt::log::error() << std::endl;
}

static void processPendingActions(ViewerState &viewerState, ParticleSystem &particleSystem,
                                  const bgfx::VertexLayout &layout,
                                  TrajectoryReader::FileType &particleFileType,
                                  SimulationBox &simulationBox,
                                  std::unique_ptr<TrajectoryReader> &trajectoryReader,
                                  std::string &loadedPath,
                                  size_t &currentFrame,
                                  size_t &totalFrames,
                                  StructureFactorResources &structureFactorResources,
                                  float particleSizeScale,
                                  uint16_t &sphereStacks, uint16_t &sphereSlices,
                                  float cutPlaneStep, float cutPlaneMinSceneZ,
                                  float cutPlaneMaxSceneZ, int &exitCode)
{
    const float sphericalCutMaxRadius =
        computeSphericalCutMaxRadius(simulationBox, viewerState.fileDimensionality);

    if ((viewerState.autoFindNeighbors
         || (viewerState.frankKasperAutoRecalculate
             && viewerState.frankKasperViewActivatedOnce))
        && !viewerState.neighborAnalysisValid)
    {
        viewerState.pendingFindNeighbors = true;
    }
    if (viewerState.rdfNeedsFullResolutionRefine && !viewerState.rdfInteractionLowResActive)
    {
        markRdfDirty(viewerState);
        viewerState.rdfNeedsFullResolutionRefine = false;
    }

    if (viewerState.rdfAuto && viewerState.rdfDirty)
    {
        viewerState.rdfPendingCompute = true;
    }

    processPendingFileOpenAction(viewerState, particleSystem, layout,
                                 particleFileType, simulationBox,
                                 trajectoryReader, structureFactorResources,
                                 loadedPath, currentFrame, totalFrames,
                                 sphereStacks, sphereSlices);

    processSelectionAndVisibilityPendingActions(viewerState, particleSystem,
                                                simulationBox, particleFileType,
                                                particleSizeScale);

    processAnalysisAndFrankKasperPendingActions(viewerState, particleSystem,
                                                simulationBox);

    if (!viewerState.structureFactorPanelOpen)
    {
        viewerState.structureFactorPendingCompute = false;
        viewerState.structureFactorInteractionLowResActive = false;
    }
    else if (viewerState.structureFactorPendingCompute)
    {
        updateStructureFactorPreview(viewerState, simulationBox,
                                     particleSystem, structureFactorResources, loadShader);
    }

    if (!viewerState.rdfPanelOpen)
    {
        viewerState.rdfPendingCompute = false;
        viewerState.rdfInteractionLowResActive = false;
        viewerState.rdfNeedsFullResolutionRefine = false;
        viewerState.rdfBatchState = {};
    }
    else if (viewerState.rdfPendingCompute)
    {
        computeRadialDistributionFunction(viewerState, simulationBox, particleSystem);
    }

    if (viewerState.pendingDescribeSelection)
    {
        printSelectedParticles(particleSystem, viewerState.selectedIds, simulationBox);
        viewerState.pendingDescribeSelection = false;
    }

    if (viewerState.pendingDescribeSelectedBondOrder)
    {
        printSelectedBondOrderParameters(viewerState, particleSystem);
        viewerState.pendingDescribeSelectedBondOrder = false;
    }

    if (viewerState.pendingAlignViewToSelection)
    {
        alignViewToSelectedParticles(viewerState, particleSystem, simulationBox);
        viewerState.pendingAlignViewToSelection = false;
    }

    if (viewerState.pendingDescribeVisibleCount)
    {
        printVisibleParticleCount(particleSystem);
        viewerState.pendingDescribeVisibleCount = false;
    }

    if (viewerState.pendingUnselect)
    {
        viewerState.selectedIds.clear();
        viewerState.pendingUnselect = false;
    }

    if (viewerState.pendingEnableCutPlane)
    {
        viewerState.cutPlaneEnabled = true;
        viewerState.cutPlaneSceneZ = 0.0f;
        viewerState.pendingEnableCutPlane = false;
        markPickBufferDirty(viewerState);
    }

    if (viewerState.pendingDisableCutPlane)
    {
        viewerState.cutPlaneEnabled = false;
        viewerState.cutPlaneSceneZ = cutPlaneMaxSceneZ;
        viewerState.pendingDisableCutPlane = false;
        markPickBufferDirty(viewerState);
    }

    if (viewerState.pendingEnableSphericalCut)
    {
        viewerState.sphericalCutEnabled = true;
        viewerState.sphericalCutRadius = sphericalCutMaxRadius;
        viewerState.pendingEnableSphericalCut = false;
        markPickBufferDirty(viewerState);
    }

    if (viewerState.pendingDisableSphericalCut)
    {
        viewerState.sphericalCutEnabled = false;
        viewerState.pendingDisableSphericalCut = false;
        markPickBufferDirty(viewerState);
    }

    if (viewerState.pendingCutPlaneStep != 0)
    {
        if (viewerState.cutPlaneEnabled)
        {
            viewerState.cutPlaneSceneZ =
                std::clamp(viewerState.cutPlaneSceneZ
                               + float(viewerState.pendingCutPlaneStep) * cutPlaneStep,
                           cutPlaneMinSceneZ, cutPlaneMaxSceneZ);
            markPickBufferDirty(viewerState);
        }
        viewerState.pendingCutPlaneStep = 0;
    }

    if (viewerState.pendingSphericalCutStep != 0)
    {
        if (viewerState.sphericalCutEnabled)
        {
            const float sphericalCutStep = bx::max(1.0e-5f, sphericalCutMaxRadius * 0.01f);
            viewerState.sphericalCutRadius =
                std::clamp(viewerState.sphericalCutRadius
                               + float(viewerState.pendingSphericalCutStep) * sphericalCutStep,
                           0.0f, sphericalCutMaxRadius);
            markPickBufferDirty(viewerState);
        }
        viewerState.pendingSphericalCutStep = 0;
    }

    viewerState.sphericalCutRadius = std::clamp(viewerState.sphericalCutRadius,
                                                0.0f, sphericalCutMaxRadius);

    const bool supportsResolutionAdjustment = particleFileType != TrajectoryReader::FileType::Polygon;
    bool changedSphereResolution = false;
    if (supportsResolutionAdjustment)
    {
        const uint16_t requestedResolution =
            static_cast<uint16_t>(std::max<int>(int(viewerState.particleResolution),
                                                int(kMinSphereStacks)));
        viewerState.particleResolution = requestedResolution;
        if (sphereStacks != requestedResolution || sphereSlices != requestedResolution)
        {
            sphereStacks = requestedResolution;
            sphereSlices = requestedResolution;
            changedSphereResolution = true;
        }
    }

    if (viewerState.pendingIncreaseSphereResolution)
    {
        if (supportsResolutionAdjustment)
        {
            sphereStacks = static_cast<uint16_t>(sphereStacks + kSphereResolutionStep);
            sphereSlices = static_cast<uint16_t>(sphereSlices + kSphereResolutionStep);
            changedSphereResolution = true;
        }
        viewerState.pendingIncreaseSphereResolution = false;
    }

    if (viewerState.pendingDecreaseSphereResolution)
    {
        if (supportsResolutionAdjustment)
        {
            const uint16_t decreasedStacks =
                static_cast<uint16_t>(sphereStacks - bx::min(sphereStacks - kMinSphereStacks,
                                                             kSphereResolutionStep));
            const uint16_t decreasedSlices =
                static_cast<uint16_t>(sphereSlices - bx::min(sphereSlices - kMinSphereSlices,
                                                             kSphereResolutionStep));
            changedSphereResolution = changedSphereResolution
                                      || decreasedStacks != sphereStacks
                                      || decreasedSlices != sphereSlices;
            sphereStacks = decreasedStacks;
            sphereSlices = decreasedSlices;
        }
        viewerState.pendingDecreaseSphereResolution = false;
    }

    viewerState.particleResolution = sphereStacks;

    if (changedSphereResolution)
    {
        const std::vector<std::vector<bx::Vec3>> emptyVoronoiPointSets;
        const std::vector<std::vector<bx::Vec3>> &voronoiPointSets =
            (trajectoryReader != nullptr) ? trajectoryReader->voronoiPointSets()
                                          : emptyVoronoiPointSets;
        particleSystem.setType(createParticleType(layout, particleFileType,
                                                  voronoiPointSets,
                                                  sphereStacks, sphereSlices));
        markAllHelperSystemsDirty(viewerState);
        markPickBufferDirty(viewerState);
        if (!hasValidParticleMeshes(particleSystem))
        {
            cvt::log::error() << "Failed to rebuild " << particleTypeName(particleFileType)
                              << " mesh for resolution "
                              << sphereStacks << "x" << sphereSlices << std::endl;
            exitCode = 1;
        }
    }
}

static void updateMouseDrivenInteraction(ViewerState &viewerState, uint16_t width,
                                         uint16_t height, float zoomedHalfWidth,
                                         float zoomedHalfHeight)
{
    if (viewerState.leftMouseDown)
    {
        if (!viewerState.leftDragActive)
        {
            const double dxSincePress = viewerState.mouseX - viewerState.leftMousePressX;
            const double dySincePress = viewerState.mouseY - viewerState.leftMousePressY;
            const double distanceSquared = dxSincePress * dxSincePress
                                           + dySincePress * dySincePress;
            if (distanceSquared >= kClickDragThresholdPixels * kClickDragThresholdPixels)
            {
                viewerState.leftDragActive = true;
                viewerState.lastMouseX = viewerState.leftMousePressX;
                viewerState.lastMouseY = viewerState.leftMousePressY;
            }
        }

        if (viewerState.leftDragActive)
        {
            const float dx = float(viewerState.mouseX - viewerState.lastMouseX);
            const float dy = float(viewerState.mouseY - viewerState.lastMouseY);
            if (viewerState.leftTranslateMode)
            {
                if (width > 0 && height > 0)
                {
                    const bx::Vec3 sceneTranslation = {
                        kMouseTranslationScale * dx * (2.0f * zoomedHalfWidth / float(width)),
                        -kMouseTranslationScale * dy * (2.0f * zoomedHalfHeight / float(height)),
                        0.0f,
                    };
                    const bx::Vec3 modelTranslation =
                        inverseRotateVector(viewerState.sceneRotation, sceneTranslation);
                    viewerState.particleTranslation.x += modelTranslation.x;
                    viewerState.particleTranslation.y += modelTranslation.y;
                    viewerState.particleTranslation.z += modelTranslation.z;
                    markBondLikeHelperSystemsDirty(viewerState);
                }
            }
            else
            {
                applySceneRotation(viewerState, -dy * kMouseRotationScale,
                                   -dx * kMouseRotationScale, 0.0f);
            }
            markPickBufferDirty(viewerState);
            viewerState.lastMouseX = viewerState.mouseX;
            viewerState.lastMouseY = viewerState.mouseY;
        }
        return;
    }

    if (viewerState.rightMouseDown)
    {
        const float dy = float(viewerState.mouseY - viewerState.lastMouseY);
        applySceneRotation(viewerState, 0.0f, 0.0f, dy * kMouseRotationScale);
        markPickBufferDirty(viewerState);
        viewerState.lastMouseX = viewerState.mouseX;
        viewerState.lastMouseY = viewerState.mouseY;
    }
}

#ifndef NDEBUG
static void drawDebugOverlay(const ParticleSystem &particleSystem,
                             const ViewerState &viewerState,
                             const PickResources &pickResources,
                             TrajectoryReader::FileType particleFileType,
                             uint16_t sphereStacks, uint16_t sphereSlices,
                             const std::string &loadedPath,
                             size_t currentFrame, size_t totalFrames)
{
    bgfx::dbgTextClear();
    bgfx::dbgTextPrintf(0, 0, 0x0f, "Rendering %d %ss with instancing.",
                        (int)particleSystem.size(), particleTypeName(particleFileType),
                        (int)particleSystem.size());
    bgfx::dbgTextPrintf(
        0, 1, 0x0f,
        "Left click toggles selection. H hides selected. Shift+H reveals all.");
    if (isSphereLikeFileType(particleFileType))
    {
        const uint32_t displayLastPickedId =
            viewerState.lastPickedId > 0u ? (viewerState.lastPickedId - 1u) : 0u;
        bgfx::dbgTextPrintf(0, 2, 0x0f, "Selected: %d  Last pick: %u  %s: %ux%u",
                            (int)viewerState.selectedIds.size(), displayLastPickedId,
                            particleTypeName(particleFileType), sphereStacks, sphereSlices);
        bgfx::dbgTextPrintf(0, 3, 0x0f,
                            "Drag rotates. Shift+drag translates. A aligns to a selected pair. D/V print info.");
    }
    else
    {
        const uint32_t displayLastPickedId =
            viewerState.lastPickedId > 0u ? (viewerState.lastPickedId - 1u) : 0u;
        bgfx::dbgTextPrintf(0, 2, 0x0f, "Selected: %d  Last pick: %u  %s: %ux%u",
                            (int)viewerState.selectedIds.size(), displayLastPickedId,
                            particleTypeName(particleFileType), sphereStacks, sphereSlices);
        bgfx::dbgTextPrintf(0, 3, 0x0f,
                            "Drag rotates. Shift+drag translates. A aligns to a selected pair. D/V print info.");
    }
    bgfx::dbgTextPrintf(0, 4, 0x0f,
                        "Enter resets rotation. B toggles box. Shift+B toggles bonds. P saves PNG.");
    bgfx::dbgTextPrintf(0, 5, 0x0f,
                        "M cycles colors. Shift+M toggles mobility. Box: %s  Mobility: %s  Bonds: %s",
                        viewerState.showBox ? "on" : "off",
                        viewerState.mobilityModeEnabled ? "on" : "off",
                        viewerState.bondModeEnabled ? "on" : "off");
    const std::string currentColorModeLabel =
        colorModeName(viewerState.colorMode, viewerState);
    bgfx::dbgTextPrintf(0, 6, 0x0f,
                        "Cut plane: %s  z=%.2f  Sphere cut: %s r=%.2f",
                        viewerState.cutPlaneEnabled ? "active" : "off",
                        viewerState.cutPlaneSceneZ,
                        viewerState.sphericalCutEnabled ? "active" : "off",
                        viewerState.sphericalCutRadius);
    bgfx::dbgTextPrintf(0, 7, 0x0f,
                        "Color: %s  Size: %.2f  Light: %.2f",
                        currentColorModeLabel.c_str(),
                        viewerState.particleSizeScale,
                        lightingScaleFromIndex(viewerState.lightingLevelIndex));
    bgfx::dbgTextPrintf(0, 8, 0x0f, "GPU picking: %s",
                        pickResources.enabled
                            ? (viewerState.pendingPickReadback
                                   ? "updating cache"
                                   : (hasValidPickBuffer(viewerState) ? "cached" : "stale"))
                            : pickResources.disableReason.c_str());
    if (!loadedPath.empty())
    {
        bgfx::dbgTextPrintf(
            0, 9, 0x0f,
            "Left/Right step frames. Shift+Left jumps first. Shift+Right jumps last.");
        bgfx::dbgTextPrintf(0, 10, 0x0f, "Loaded %s (frame %d/%d)", loadedPath.c_str(),
                            (int)currentFrame + 1, (int)totalFrames);
    }
}
#endif

static bx::Vec3 transformPoint(const float *transform, const bx::Vec3 &point)
{
    return {
        point.x * transform[0] + point.y * transform[4] + point.z * transform[8]
            + transform[12],
        point.x * transform[1] + point.y * transform[5] + point.z * transform[9]
            + transform[13],
        point.x * transform[2] + point.y * transform[6] + point.z * transform[10]
            + transform[14],
    };
}

static void renderSimulationBoxWireframe(bgfx::ViewId viewId,
                                         bgfx::ProgramHandle program,
                                         const bgfx::VertexLayout &layout,
                                         const SimulationBox &simulationBox,
                                         const float *sceneTransform)
{
    if (!bgfx::isValid(program))
    {
        return;
    }

    constexpr uint32_t kBoxEdgeColor = 0xff000000u;
    constexpr uint32_t kCircleSegmentCount = 96u;
    constexpr uint32_t kEdgeCount = 12u;
    constexpr uint32_t kVertexCount = kEdgeCount * 2u;
    constexpr uint8_t kEdges[kEdgeCount][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };

    bgfx::TransientVertexBuffer tvb;
    if (simulationBox.shape() == SimulationBox::Shape::Spherical)
    {
        const uint32_t circleVertexCount = kCircleSegmentCount * 2u;
        if (!bgfx::getAvailTransientVertexBuffer(circleVertexCount, layout))
        {
            return;
        }

        bgfx::allocTransientVertexBuffer(&tvb, circleVertexCount, layout);
        auto *vertices = reinterpret_cast<LineVertex *>(tvb.data);
        const bx::Vec3 transformedCenter = transformPoint(sceneTransform, simulationBox.center());
        const float radius = simulationBox.renderRadius();
        for (uint32_t segmentIndex = 0; segmentIndex < kCircleSegmentCount; ++segmentIndex)
        {
            const float angle0 = 2.0f * bx::kPi * float(segmentIndex)
                                 / float(kCircleSegmentCount);
            const float angle1 = 2.0f * bx::kPi * float(segmentIndex + 1u)
                                 / float(kCircleSegmentCount);
            LineVertex &vertex0 = vertices[segmentIndex * 2u];
            vertex0.x = transformedCenter.x + radius * std::cos(angle0);
            vertex0.y = transformedCenter.y + radius * std::sin(angle0);
            vertex0.z = transformedCenter.z;
            vertex0.abgr = kBoxEdgeColor;

            LineVertex &vertex1 = vertices[segmentIndex * 2u + 1u];
            vertex1.x = transformedCenter.x + radius * std::cos(angle1);
            vertex1.y = transformedCenter.y + radius * std::sin(angle1);
            vertex1.z = transformedCenter.z;
            vertex1.abgr = kBoxEdgeColor;
        }
    }
    else
    {
        const bx::Vec3 &minBounds = simulationBox.minBounds();
        const bx::Vec3 &maxBounds = simulationBox.maxBounds();
        const bx::Vec3 corners[8] = {
            {minBounds.x, minBounds.y, minBounds.z},
            {maxBounds.x, minBounds.y, minBounds.z},
            {maxBounds.x, maxBounds.y, minBounds.z},
            {minBounds.x, maxBounds.y, minBounds.z},
            {minBounds.x, minBounds.y, maxBounds.z},
            {maxBounds.x, minBounds.y, maxBounds.z},
            {maxBounds.x, maxBounds.y, maxBounds.z},
            {minBounds.x, maxBounds.y, maxBounds.z},
        };

        if (!bgfx::getAvailTransientVertexBuffer(kVertexCount, layout))
        {
            return;
        }

        bgfx::allocTransientVertexBuffer(&tvb, kVertexCount, layout);
        auto *vertices = reinterpret_cast<LineVertex *>(tvb.data);
        for (uint32_t edgeIndex = 0; edgeIndex < kEdgeCount; ++edgeIndex)
        {
            for (uint32_t endpointIndex = 0; endpointIndex < 2; ++endpointIndex)
            {
                const bx::Vec3 transformedPoint =
                    transformPoint(sceneTransform, corners[kEdges[edgeIndex][endpointIndex]]);
                LineVertex &vertex = vertices[edgeIndex * 2u + endpointIndex];
                vertex.x = transformedPoint.x;
                vertex.y = transformedPoint.y;
                vertex.z = transformedPoint.z;
                vertex.abgr = kBoxEdgeColor;
            }
        }
    }

    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL
                   | BGFX_STATE_MSAA | BGFX_STATE_PT_LINES);
    bgfx::submit(viewId, program);
}

int main(int argc, char **argv)
{
    ViewerState viewerState;
    ScreenshotCallback screenshotCallback;
    int exitCode = 0;
    s_resourceSearchRoots = buildResourceSearchRoots(argc > 0 ? argv[0] : nullptr);

    {
        const fs::path configPath = resolveResourcePath("cvt.ini");
        if (!configPath.empty())
        {
            const ViewerConfig config = loadViewerConfig(configPath);
            viewerState.showUi                              = config.showUi;
            viewerState.showBox                             = config.showBox;
            viewerState.basicControlsDefaultOpen            = config.basicControlsOpen;
            viewerState.lightingLevelIndex                  = static_cast<uint8_t>(config.lightingLevel);
            viewerState.structureFactorUseGpu               = config.structureFactorUseGpu;
            viewerState.structureFactorSuppressCentralPeak  = config.structureFactorSuppressCentralPeak;
            viewerState.structureFactorBatchModesPerStep    = config.structureFactorCpuModesPerStep;
            viewerState.structureFactorGpuBatchRowsPerStep  = config.structureFactorGpuRowsPerStep;
        }
    }

    glfwSetErrorCallback(glfw_errorCallback);
    GlfwLibraryGuard glfwGuard;
    if (!glfwInit())
    {
        return 1;
    }
    glfwGuard.initialized = true;

    const ViewerWindowCallbacks windowCallbacks = {
        glfw_keyCallback,
        glfw_charCallback,
        glfw_mouseCallback,
        glfw_mouseButtonCallback,
        glfw_scrollCallback,
        glfw_dropCallback,
    };
    GlfwWindowHandle window = createViewerWindow(viewerState, windowCallbacks);
    if (!window)
    {
        return 1;
    }

    // Call bgfx::renderFrame before bgfx::init to signal to bgfx not to create a render thread.
    // Most graphics APIs must be used on the same thread that created the window.
    bgfx::renderFrame();

    bgfx::Init init;
    configureBgfxPlatformData(init, window.get());

    int width = 0;
    int height = 0;
    glfwGetWindowSize(window.get(), &width, &height);
    init.callback = &screenshotCallback;
    init.resolution.width = static_cast<uint32_t>(width);
    init.resolution.height = static_cast<uint32_t>(height);
    init.resolution.reset = BGFX_RESET_VSYNC;
    // Large ImPlot scatter plots can exhaust bgfx transient buffers with default limits.
    init.limits.maxTransientVbSize = 16u * 1024u * 1024u;
    init.limits.maxTransientIbSize = 8u * 1024u * 1024u;

    BgfxLibraryGuard bgfxGuard;
    if (!initializeBgfxRenderer(init))
    {
        return 1;
    }
    bgfxGuard.initialized = true;

    {
        bgfx::VertexLayout layout;
        bgfx::VertexLayout lineLayout;
        PosNormalVertex::init(layout);
        LineVertex::init(lineLayout);
        uint16_t sphereStacks = kDefaultSphereStacks;
        uint16_t sphereSlices = kDefaultSphereSlices;

        SimulationBox simulationBox({-7.5f, -7.5f, -7.5f}, {7.5f, 7.5f, 7.5f});
        simulationBox.setPeriodic(true, true, true);

        ParticleSystem particleSystem(
            createSphereParticleType(layout, sphereStacks, sphereSlices));
        ParticleSystem mobilitySystem(createArrowParticleType(layout, kDefaultArrowSlices));
        ParticleSystem bondDiagramCoreSystem(
            createSphereParticleType(layout, kBondDiagramSphereStacks, kBondDiagramSphereSlices));
        ParticleSystem bondDiagramMarkerSystem(createOctahedronParticleType(layout));
        PatchRenderSystems patchRenderSystems;
        BondRenderSystems bondRenderSystems;
        BondRenderSystems nearestNeighborRenderSystems;
        PolygonRenderSystems polygonRenderSystems;
        BondDiagramResources bondDiagramResources;
        StructureFactorResources structureFactorResources;
        TrajectoryReader::FileType particleFileType = TrajectoryReader::FileType::Sphere;
        PickResources pickResources;
        AppProgramHandles gpuResources;
        ImGuiGuard imGuiGuard;

        const bgfx::ShaderHandle mainVertexShader = loadShader("shaders/vs_instancing.bin");
        const bgfx::ShaderHandle mainFragmentShader = loadShader("shaders/fs_instancing.bin");
        gpuResources.mainProgram =
            bgfx::createProgram(mainVertexShader, mainFragmentShader, true);

        const bgfx::ShaderHandle pickVertexShader = loadShader("shaders/vs_picking.bin");
        const bgfx::ShaderHandle pickFragmentShader = loadShader("shaders/fs_picking.bin");
        gpuResources.pickProgram =
            bgfx::createProgram(pickVertexShader, pickFragmentShader, true);

        const bgfx::ShaderHandle lineVertexShader = loadShader("shaders/vs_lines.bin");
        const bgfx::ShaderHandle lineFragmentShader = loadShader("shaders/fs_lines.bin");
        gpuResources.lineProgram =
            bgfx::createProgram(lineVertexShader, lineFragmentShader, true);

        gpuResources.lightDirectionUniform =
            bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
        gpuResources.lightScaleUniform =
            bgfx::createUniform("u_lightScale", bgfx::UniformType::Vec4);

        size_t currentFrame = 0;
        size_t totalFrames = 1;
        std::string loadedPath;
        std::unique_ptr<TrajectoryReader> trajectoryReader;

        if (argc > 1)
        {
            openTrajectoryFile(argv[1], viewerState, layout,
                               sphereStacks, sphereSlices, trajectoryReader,
                               particleSystem, particleFileType, simulationBox,
                               structureFactorResources, loadedPath,
                               currentFrame, totalFrames);
        }
        else
        {
            viewerState.fileOpenStatusMessage =
                "Drop a trajectory file into the window to open it.";
            cvt::log::info() << viewerState.fileOpenStatusMessage << std::endl;
        }

        bgfx::setViewClear(kMainView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                           0xffffffff, 1.0f, 0);
        bgfx::setViewClear(kPickView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                           0x00000000, 1.0f, 0);
        bgfx::setViewClear(kBondDiagramView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                           0xffffffff, 1.0f, 0);

        const bool uiAvailable = ImGuiBgfx::create(kUiView);
        imGuiGuard.initialized = uiAvailable;
        if (!uiAvailable)
        {
            cvt::log::error() << "Dear ImGui UI unavailable; continuing without UI." << std::endl;
        }

        viewerState.uiPanelWidth =
            computeUiPanelWidth(static_cast<uint16_t>(width), viewerState.showUi);
        viewerState.renderViewportWidth =
            computeRenderViewportWidth(static_cast<uint16_t>(width), viewerState.showUi);
        viewerState.renderViewportHeight = static_cast<uint16_t>(bx::max(height, 1));

        const float initialAspectRatio =
            height > 0 ? float(viewerState.renderViewportWidth) / float(height) : 1.0f;
        float cameraDistance = computeInitialCameraDistance(simulationBox);
        bx::Vec3 cameraPos = {0.0f, 0.0f, cameraDistance};
        const float orthoBaseHalfHeight =
            computeInitialOrthoHalfHeight(simulationBox, initialAspectRatio);
        const float nearPlane = 0.1f;
        const float farPlane = computeInitialFarPlane(cameraDistance, simulationBox);
        const float cutPlaneStep = computeCutPlaneStep(simulationBox);
        const float cutPlaneMinSceneZ =
            -0.5f * std::sqrt(simulationBox.size().x * simulationBox.size().x
                              + simulationBox.size().y * simulationBox.size().y
                              + simulationBox.size().z * simulationBox.size().z);
        const float cutPlaneMaxSceneZ = cameraDistance;
        const bx::Vec3 lightDirection = bx::normalize(kInitialLightDirection);

        bgfx::setViewRect(kMainView, 0, 0, viewerState.renderViewportWidth,
                          viewerState.renderViewportHeight);
        if (!createPickResources(pickResources, viewerState.renderViewportWidth,
                                 viewerState.renderViewportHeight))
        {
            cvt::log::error() << "GPU picking disabled: " << pickResources.disableReason << std::endl;
        }
        if (!createBondDiagramResources(bondDiagramResources, kBondDiagramPreviewSize,
                                        kBondDiagramPreviewSize))
        {
            cvt::log::error() << "Bond diagram preview unavailable: "
                              << bondDiagramResources.disableReason << std::endl;
        }

        if (exitCode == 0
            && (!hasValidParticleMeshes(particleSystem)
                || !bgfx::isValid(gpuResources.mainProgram)))
        {
            cvt::log::error() << "INVALID BGFX RESOURCE: particleMeshes="
                              << hasValidParticleMeshes(particleSystem)
                              << " program=" << bgfx::isValid(gpuResources.mainProgram)
                              << std::endl;
            exitCode = 1;
        }
        if (exitCode == 0 && !bgfx::isValid(gpuResources.lineProgram))
        {
            cvt::log::error() << "Wireframe box shaders unavailable" << std::endl;
            exitCode = 1;
        }
        if (exitCode == 0 && !bgfx::isValid(gpuResources.pickProgram))
        {
            pickResources.enabled = false;
            if (pickResources.disableReason.empty())
            {
                pickResources.disableReason = "pick shaders unavailable";
            }
        }

        uint32_t lastSubmittedFrame = 0;
        double lastUiFrameTime = glfwGetTime();
        double lastBackgroundRenderTime = glfwGetTime();
        constexpr double kBackgroundEventWaitSeconds = 0.10;
        constexpr double kBackgroundFrameIntervalSeconds = 0.10;

        while (exitCode == 0 && !glfwWindowShouldClose(window.get()))
        {
            const bool windowFocused =
                glfwGetWindowAttrib(window.get(), GLFW_FOCUSED) == GLFW_TRUE;
            const bool windowIconified =
                glfwGetWindowAttrib(window.get(), GLFW_ICONIFIED) == GLFW_TRUE;
            const bool backgroundMode = windowIconified || !windowFocused;

            if (backgroundMode)
            {
                glfwWaitEventsTimeout(kBackgroundEventWaitSeconds);
            }
            else
            {
                glfwPollEvents();
            }

            if (pickResources.enabled && viewerState.pendingPickReadback
                && lastSubmittedFrame >= viewerState.pendingReadbackFrame)
            {
                if (viewerState.pendingPickRevision == viewerState.pickSceneRevision)
                {
                    viewerState.cachedPickRevision = viewerState.pendingPickRevision;
                    viewerState.pickBufferValid = true;
                    resolvePendingPickRequest(viewerState, pickResources);
                }
                viewerState.pendingPickReadback = false;
            }

            handleWindowResize(window.get(), width, height, viewerState, pickResources);
            handleTrajectoryFrameChange(viewerState, currentFrame, totalFrames, loadedPath,
                                        trajectoryReader.get(), particleSystem, simulationBox);
            processPendingActions(viewerState, particleSystem, layout, particleFileType,
                                  simulationBox, trajectoryReader, loadedPath,
                                  currentFrame, totalFrames, structureFactorResources,
                                  viewerState.particleSizeScale,
                                  sphereStacks, sphereSlices,
                                  cutPlaneStep, cutPlaneMinSceneZ,
                                  cutPlaneMaxSceneZ, exitCode);

            bool shouldRenderFrame = true;
            if (backgroundMode)
            {
                const double now = glfwGetTime();
                const bool backgroundFrameDue =
                    (now - lastBackgroundRenderTime) >= kBackgroundFrameIntervalSeconds;
                const bool requiresImmediateFrame = viewerState.pendingPickReadback
                                                    || viewerState.pendingPickRequest
                                                    || viewerState.pendingScreenshotRequest;
                shouldRenderFrame = backgroundFrameDue || requiresImmediateFrame;
                if (shouldRenderFrame)
                {
                    lastBackgroundRenderTime = now;
                }
            }

            if (!shouldRenderFrame)
            {
                continue;
            }

            float view[16];
            float proj[16];
            bx::mtxLookAt(view, cameraPos, s_cameraTarget, bx::Vec3{0.0f, 1.0f, 0.0f},
                          bx::Handedness::Right);
            bgfx::setViewRect(kMainView, 0, 0, viewerState.renderViewportWidth,
                              viewerState.renderViewportHeight);
            const float currentAspectRatio =
                height > 0 ? float(viewerState.renderViewportWidth) / float(height) : 1.0f;
            const float zoomedHalfHeight = orthoBaseHalfHeight * viewerState.orthoZoom;
            const float zoomedHalfWidth = currentAspectRatio * zoomedHalfHeight;
            updateMouseDrivenInteraction(viewerState, viewerState.renderViewportWidth,
                                         viewerState.renderViewportHeight,
                                         zoomedHalfWidth, zoomedHalfHeight);
            bx::mtxOrtho(proj, -zoomedHalfWidth, zoomedHalfWidth, -zoomedHalfHeight,
                         zoomedHalfHeight, nearPlane, farPlane, 0.0f,
                         bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
            bgfx::setViewTransform(kMainView, view, proj);

            beginViewerUiFrame(window.get(), width, height, viewerState,
                               lastUiFrameTime, s_uiScrollX, s_uiScrollY);

            drawViewerControls(viewerState, particleSystem, &bondDiagramResources,
                               &structureFactorResources, particleFileType,
                               simulationBox,
                               loadedPath, currentFrame, totalFrames,
                               static_cast<uint16_t>(width), static_cast<uint16_t>(height),
                               cutPlaneMinSceneZ, cutPlaneMaxSceneZ);

            const float lightDir[4] = {
                lightDirection.x,
                lightDirection.y,
                lightDirection.z,
                1.0f,
            };
            bgfx::setUniform(gpuResources.lightDirectionUniform, lightDir, 1);
            const float lightingScale = lightingScaleFromIndex(viewerState.lightingLevelIndex);
            const float lightScale[4] = {lightingScale, 0.0f, 0.0f, 0.0f};
            bgfx::setUniform(gpuResources.lightScaleUniform, lightScale, 1);

            const bx::Vec3 boxCenter = simulationBox.center();
            float sceneTranslateMtx[16];
            bx::mtxTranslate(sceneTranslateMtx, -boxCenter.x, -boxCenter.y, -boxCenter.z);

            float sceneTransform[16];
            bx::mtxMul(sceneTransform, sceneTranslateMtx, viewerState.sceneRotation);

            applyColorMode(particleSystem, viewerState.colorMode,
                           particleFileType == TrajectoryReader::FileType::Rod, false,
                           viewerState.particleColorStatsCache, viewerState);
            applyAnalysisColorMode(particleSystem, viewerState);
            if (viewerState.mobilitySystemDirty)
            {
                rebuildMobilitySystem(particleSystem, mobilitySystem, viewerState,
                                      simulationBox);
                viewerState.mobilitySystemDirty = false;
            }
            applyColorMode(mobilitySystem, viewerState.colorMode, true, true,
                           viewerState.mobilityColorStatsCache, viewerState);
            applyAnalysisColorMode(mobilitySystem, particleSystem, viewerState);
            updateAuxiliaryRenderSystemsIfNeeded(viewerState, layout, sphereStacks,
                                                 sphereSlices, particleFileType,
                                                 particleSystem, bondDiagramCoreSystem,
                                                 bondDiagramMarkerSystem,
                                                 patchRenderSystems, bondRenderSystems,
                                                 nearestNeighborRenderSystems,
                                                 polygonRenderSystems, simulationBox);

            renderActiveScene(kMainView, gpuResources.mainProgram, viewerState,
                              particleFileType, simulationBox, sceneTransform,
                              particleSystem, mobilitySystem,
                              patchRenderSystems, bondRenderSystems,
                              nearestNeighborRenderSystems, polygonRenderSystems,
                              false);

            if (viewerState.showBox)
            {
                renderSimulationBoxWireframe(kMainView, gpuResources.lineProgram, lineLayout,
                                             simulationBox, sceneTransform);
            }

            if (bondDiagramResources.enabled && viewerState.bondDiagramRenderRequested
                && viewerState.neighborAnalysisValid
                && (viewerState.bondDiagramViewDirty || viewerState.bondDiagramGeometryDirty))
            {
                renderBondDiagramPreview(viewerState, bondDiagramResources,
                                         bondDiagramCoreSystem, bondDiagramMarkerSystem,
                                         gpuResources.mainProgram);
                viewerState.bondDiagramViewDirty = false;
            }

            if (pickResources.enabled && viewerState.pendingPickRequest
                && hasValidPickBuffer(viewerState))
            {
                resolvePendingPickRequest(viewerState, pickResources);
            }

            const bool shouldRefreshPickBuffer =
                pickResources.enabled && bgfx::isValid(gpuResources.pickProgram)
                && !viewerState.pendingPickReadback && !hasValidPickBuffer(viewerState)
                && !viewerState.leftMouseDown && !viewerState.rightMouseDown
                && !backgroundMode;
            if (shouldRefreshPickBuffer)
            {
                bgfx::setViewRect(kPickView, 0, 0, pickResources.width, pickResources.height);
                bgfx::setViewFrameBuffer(kPickView, pickResources.frameBuffer);
                bgfx::setViewTransform(kPickView, view, proj);
                renderActiveScene(kPickView, gpuResources.pickProgram, viewerState,
                                  particleFileType, simulationBox, sceneTransform,
                                  particleSystem, mobilitySystem,
                                  patchRenderSystems, bondRenderSystems,
                                  nearestNeighborRenderSystems, polygonRenderSystems,
                                  true);

                bgfx::blit(kBlitView, pickResources.readbackTexture, 0, 0,
                           pickResources.renderColorTexture);

                viewerState.pendingPickRevision = viewerState.pickSceneRevision;
                viewerState.pendingReadbackFrame =
                    bgfx::readTexture(pickResources.readbackTexture,
                                      pickResources.readbackData.data());
                viewerState.pendingPickReadback = true;
            }

#ifndef NDEBUG
            drawDebugOverlay(particleSystem, viewerState, pickResources, particleFileType,
                             sphereStacks, sphereSlices,
                             loadedPath, currentFrame, totalFrames);
            bgfx::setDebug(viewerState.showStats ? BGFX_DEBUG_STATS : BGFX_DEBUG_TEXT);
#else
            bgfx::setDebug(BGFX_DEBUG_NONE);
#endif
            ImGuiBgfx::endFrame();
            if (viewerState.pendingScreenshotRequest)
            {
                const std::string screenshotPath = makeTimestampedScreenshotPath();
                screenshotCallback.queueViewportCrop(screenshotPath,
                                                     viewerState.renderViewportWidth);
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE, screenshotPath.c_str());
                viewerState.pendingScreenshotRequest = false;
            }

            lastSubmittedFrame = bgfx::frame();
        }

        destroyPickResources(pickResources);
        destroyBondDiagramResources(bondDiagramResources);
        destroyStructureFactorResources(structureFactorResources);
    }

    return exitCode;
}