#include "ArrowType.h"
#include "ColorPalette.h"
#include "CubeType.h"
#include "CylinderType.h"
#include "ImGuiBgfx.h"
#include "Mesh.h"
#include "PatchCapType.h"
#include "PatchConeType.h"
#include "PatchPlacement.h"
#include "Particle.h"
#include "ParticleSystem.h"
#include "PolygonType.h"
#include "RodType.h"
#include "ScreenshotSupport.h"
#include "SimulationBox.h"
#include "SphereType.h"
#include "TrajectoryReader.h"
#include "ViewerSupport.h"
#include "ViewerUi.h"

#include <bx/bx.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>
#include <dear-imgui/imgui.h>
#include <GLFW/glfw3.h>
#if BX_PLATFORM_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#elif BX_PLATFORM_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#elif BX_PLATFORM_OSX
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>

#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
#include <unistd.h>
#include <limits.h>
#elif BX_PLATFORM_OSX
#include <mach-o/dyld.h>
#elif BX_PLATFORM_WINDOWS
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

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

} // namespace

// ---- Load shader ----
bgfx::ShaderHandle loadShader(const char *path)
{
    const fs::path shaderPath = resolveResourcePath(fs::path(path));
    std::ifstream file(shaderPath.empty() ? fs::path(path) : shaderPath, std::ios::binary);
    if (!file)
    {
        std::cerr << "Failed to open shader: " << path;
        if (!s_resourceSearchRoots.empty())
        {
            std::cerr << " (searched";
            for (const fs::path &root : s_resourceSearchRoots)
            {
                std::cerr << ' ' << (root / path).string();
            }
            std::cerr << ')';
        }
        std::cerr << std::endl;
        return BGFX_INVALID_HANDLE;
    }
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0);

    const bgfx::Memory *mem = bgfx::alloc(size);
    file.read((char *)mem->data, size);

    return bgfx::createShader(mem);
}

static const bx::Vec3 s_cameraTarget = {0.0f, 0.0f, 0.0f};
static bx::Vec3 s_lightDir = {0.0f, 0.0f, 5.0f};
// World-space direction from the shaded point toward the light source.

namespace
{

constexpr float kZoomPerWheelStep = 0.9f;
constexpr float kMinOrthoZoom = 0.05f;
constexpr float kMaxOrthoZoom = 100.0f;
constexpr float kMouseRotationScale = 0.01f;
constexpr float kMouseTranslationScale = 2.0f;
constexpr double kClickDragThresholdPixels = 4.0;
constexpr bgfx::ViewId kMainView = 0;
constexpr bgfx::ViewId kPickView = 1;
constexpr bgfx::ViewId kBlitView = 2;
constexpr bgfx::ViewId kUiView = 3;
constexpr float kSphereRadius = 1.0f;
constexpr uint16_t kDefaultArrowSlices = 12;
constexpr uint16_t kDefaultSphereStacks = 10;
constexpr uint16_t kDefaultSphereSlices = 10;
constexpr uint16_t kMinSphereStacks = 4;
constexpr uint16_t kMinSphereSlices = 4;
constexpr uint16_t kSphereResolutionStep = 2;
constexpr float kOrientationEpsilon = 1.0e-6f;
constexpr float kParticleSizeScaleStep = 0.01f;
constexpr float kMinParticleSizeScale = 0.01f;
constexpr uint8_t kLightingLevelCount = 30u;
constexpr std::array<float, 4> kPatchColor = {0.65f, 0.65f, 0.65f, 1.0f};
float s_uiScrollX = 0.0f;
float s_uiScrollY = 0.0f;
// Particle meshes are authored so triangles wind counter-clockwise when viewed from
// outside the particle. BGFX_STATE_FRONT_CCW therefore classifies those outward-facing
// triangles as front faces, and BGFX_STATE_CULL_CCW maps to back-face culling on the
// OpenGL backend used here. In effect, closed particle surfaces render only their
// outward-facing triangles while discarding inward-facing triangles.
constexpr uint64_t kParticleRenderState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                                          | BGFX_STATE_WRITE_Z
                                          | BGFX_STATE_DEPTH_TEST_LESS
                                          | BGFX_STATE_CULL_CCW
                                          | BGFX_STATE_FRONT_CCW
                                          | BGFX_STATE_MSAA;

struct PatchCapSystem
{
    float cosHalfAngle = 1.0f;
    std::unique_ptr<ParticleSystem> system;
};

struct PatchRenderSystems
{
    std::unique_ptr<ParticleSystem> coneSystem;
    std::vector<PatchCapSystem> capSystems;
    uint16_t coneSlices = 0u;
    uint16_t capStacks = 0u;
    uint16_t capSlices = 0u;
};

struct BondRenderSystems
{
    std::unique_ptr<ParticleSystem> cylinderSystem;
    std::unique_ptr<ParticleSystem> nodeSystem;
    uint16_t cylinderSlices = 0u;
    uint16_t sphereStacks = 0u;
    uint16_t sphereSlices = 0u;
};

struct PolygonRenderSystem
{
    uint16_t sideCount = 0u;
    std::unique_ptr<ParticleSystem> system;
};

struct PolygonRenderSystems
{
    std::vector<PolygonRenderSystem> systems;
};

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

std::unique_ptr<ParticleType> createSphereParticleType(const bgfx::VertexLayout &layout,
                                                       uint16_t stacks,
                                                       uint16_t slices)
{
    return std::make_unique<SphereType>(layout, kSphereRadius, stacks, slices);
}

std::unique_ptr<ParticleType> createArrowParticleType(const bgfx::VertexLayout &layout,
                                                      uint16_t slices)
{
    return std::make_unique<ArrowType>(layout, slices);
}

std::unique_ptr<ParticleType> createCylinderParticleType(const bgfx::VertexLayout &layout,
                                                         uint16_t slices)
{
    return std::make_unique<CylinderType>(layout, slices);
}

std::unique_ptr<ParticleType> createRodParticleType(const bgfx::VertexLayout &layout,
                                                    uint16_t stacks,
                                                    uint16_t slices)
{
    return std::make_unique<RodType>(layout, stacks, slices);
}

std::unique_ptr<ParticleType> createCubeParticleType(const bgfx::VertexLayout &layout)
{
    return std::make_unique<CubeType>(layout);
}

std::unique_ptr<ParticleType> createPolygonParticleType(const bgfx::VertexLayout &layout,
                                                        uint16_t sideCount)
{
    return std::make_unique<PolygonType>(layout, sideCount);
}

std::unique_ptr<ParticleType> createParticleType(const bgfx::VertexLayout &layout,
                                                 TrajectoryReader::FileType fileType,
                                                 uint16_t stacks,
                                                 uint16_t slices)
{
    if (fileType == TrajectoryReader::FileType::Sphere
        || fileType == TrajectoryReader::FileType::Disk)
    {
        return createSphereParticleType(layout, stacks, slices);
    }
    if (fileType == TrajectoryReader::FileType::Rod)
    {
        return createRodParticleType(layout, stacks, slices);
    }
    if (fileType == TrajectoryReader::FileType::Cube)
    {
        return createCubeParticleType(layout);
    }
    if (fileType == TrajectoryReader::FileType::Polygon)
    {
        return createPolygonParticleType(layout, 3u);
    }

    return createSphereParticleType(layout, stacks, slices);
}

bool isSphereLikeFileType(TrajectoryReader::FileType fileType)
{
    return fileType == TrajectoryReader::FileType::Sphere
           || fileType == TrajectoryReader::FileType::Disk;
}

bool isPatchyFileType(TrajectoryReader::FileType fileType)
{
    return fileType == TrajectoryReader::FileType::Patchy
           || fileType == TrajectoryReader::FileType::PatchyLegacy
           || fileType == TrajectoryReader::FileType::Patchy2D;
}

bool isPolygonFileType(TrajectoryReader::FileType fileType)
{
    return fileType == TrajectoryReader::FileType::Polygon;
}

uint16_t polygonSideCount(const Particle &particle)
{
    return static_cast<uint16_t>(std::max(0.0f, std::round(particle.sizeParams[1])));
}

std::array<float, 4> orientationColor(const bx::Vec3 &direction)
{
    const float length = bx::length(direction);
    if (length <= kOrientationEpsilon)
    {
        return colorFromLetter('A');
    }

    const float inverseLength = 1.0f / length;
    return {std::abs(direction.x) * inverseLength, std::abs(direction.y) * inverseLength,
            std::abs(direction.z) * inverseLength, 1.0f};
}

std::array<float, 4> resolveParticleColor(const Particle &particle, size_t particleIndex,
                                         ColorMode colorMode,
                                         const std::vector<PatchyParticleData> *patchMetadata,
                                         bool supportsOrientationMode,
                                         bool uniformUsesOrientation)
{
    switch (colorMode)
    {
    case ColorMode::FileDefault:
        return particle.baseColor;
    case ColorMode::PaletteCycle:
        return colorFromPaletteIndex(particleIndex);
    case ColorMode::Uniform:
        return uniformUsesOrientation ? orientationColor(particle.direction)
                                      : colorFromLetter('A');
    case ColorMode::Orientation:
        return supportsOrientationMode ? orientationColor(particle.direction)
                                       : colorFromLetter('A');
    case ColorMode::BondCount:
        if (patchMetadata != nullptr && particleIndex < patchMetadata->size())
        {
            const size_t bondCount = static_cast<size_t>(std::count_if(
                (*patchMetadata)[particleIndex].bondIds.begin(),
                (*patchMetadata)[particleIndex].bondIds.end(),
                [](int32_t bondId) { return bondId >= 0; }));
            return colorFromLetter(static_cast<char>('A' + (bondCount % kParticlePaletteColorCount)));
        }
        return colorFromLetter('A');
    case ColorMode::Count:
        break;
    }

    return particle.baseColor;
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

void applyColorMode(ParticleSystem &particleSystem, ColorMode colorMode,
                    bool supportsOrientationMode, bool uniformUsesOrientation)
{
    std::vector<Particle> &particles = particleSystem.particles();
    const std::vector<PatchyParticleData> *patchMetadata =
        particleSystem.hasPatchyMetadata() ? &particleSystem.patchyMetadata() : nullptr;
    for (size_t index = 0; index < particles.size(); ++index)
    {
        Particle &particle = particles[index];
        particle.color = resolveParticleColor(particle, index, colorMode,
                                              patchMetadata,
                                              supportsOrientationMode,
                                              uniformUsesOrientation);
    }
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
    std::vector<uint32_t> sortedIds(selectedIds.begin(), selectedIds.end());
    std::sort(sortedIds.begin(), sortedIds.end());

    std::cout << "Selected particle IDs:";
    for (uint32_t particleId : sortedIds)
    {
        std::cout << ' ' << particleId;
    }
    std::cout << std::endl;

    if (sortedIds.size() != 2u)
    {
        return;
    }

    const Particle *firstParticle = findParticleById(particleSystem, sortedIds[0]);
    const Particle *secondParticle = findParticleById(particleSystem, sortedIds[1]);
    if (firstParticle == nullptr || secondParticle == nullptr)
    {
        std::cout << "Could not resolve both selected particle IDs in the current frame."
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
    std::cout << std::fixed << std::setprecision(6)
              << "Center distance(" << sortedIds[0] << ", " << sortedIds[1]
              << "): " << centerDistance << '\n'
              << "Radius sum(" << sortedIds[0] << ", " << sortedIds[1]
              << "): " << radiusSum << std::endl;
}

bx::Vec3 rotatePatchDirection(const std::array<float, 9> &rotationMatrix,
                             const bx::Vec3 &direction)
{
    return {
        rotationMatrix[0] * direction.x + rotationMatrix[1] * direction.y
            + rotationMatrix[2] * direction.z,
        rotationMatrix[3] * direction.x + rotationMatrix[4] * direction.y
            + rotationMatrix[5] * direction.z,
        rotationMatrix[6] * direction.x + rotationMatrix[7] * direction.y
            + rotationMatrix[8] * direction.z,
    };
}

PatchCapSystem *findPatchCapSystem(PatchRenderSystems &patchRenderSystems, float cosHalfAngle)
{
    for (PatchCapSystem &capSystem : patchRenderSystems.capSystems)
    {
        if (std::abs(capSystem.cosHalfAngle - cosHalfAngle) <= 1.0e-6f)
        {
            return &capSystem;
        }
    }

    return nullptr;
}

void ensurePatchRenderSystems(const bgfx::VertexLayout &layout, uint16_t sphereStacks,
                              uint16_t sphereSlices, const ParticleSystem &particleSystem,
                              PatchRenderSystems &patchRenderSystems)
{
    if (!particleSystem.hasPatchyMetadata())
    {
        patchRenderSystems.coneSystem.reset();
        patchRenderSystems.capSystems.clear();
        return;
    }

    if (!patchRenderSystems.coneSystem || patchRenderSystems.coneSlices != sphereSlices)
    {
        patchRenderSystems.coneSystem = std::make_unique<ParticleSystem>(
            std::make_unique<PatchConeType>(layout, sphereSlices));
        patchRenderSystems.coneSlices = sphereSlices;
    }

    std::vector<float> uniqueAngles;
    for (const PatchyParticleData &patchData : particleSystem.patchyMetadata())
    {
        const bool alreadyPresent = std::any_of(
            uniqueAngles.begin(), uniqueAngles.end(),
            [angle = patchData.cosHalfAngle](float existingAngle) {
                return std::abs(existingAngle - angle) <= 1.0e-6f;
            });
        if (!alreadyPresent)
        {
            uniqueAngles.push_back(patchData.cosHalfAngle);
        }
    }
    std::sort(uniqueAngles.begin(), uniqueAngles.end());

    bool mustRebuildCapSystems = patchRenderSystems.capStacks != sphereStacks
                                 || patchRenderSystems.capSlices != sphereSlices
                                 || patchRenderSystems.capSystems.size() != uniqueAngles.size();
    if (!mustRebuildCapSystems)
    {
        for (size_t index = 0; index < uniqueAngles.size(); ++index)
        {
            if (std::abs(patchRenderSystems.capSystems[index].cosHalfAngle - uniqueAngles[index])
                > 1.0e-6f)
            {
                mustRebuildCapSystems = true;
                break;
            }
        }
    }

    if (mustRebuildCapSystems)
    {
        patchRenderSystems.capSystems.clear();
        patchRenderSystems.capSystems.reserve(uniqueAngles.size());
        for (float cosHalfAngle : uniqueAngles)
        {
            patchRenderSystems.capSystems.push_back(PatchCapSystem{
                .cosHalfAngle = cosHalfAngle,
                .system = std::make_unique<ParticleSystem>(
                    std::make_unique<PatchCapType>(layout, cosHalfAngle, sphereStacks,
                                                   sphereSlices)),
            });
        }
        patchRenderSystems.capStacks = sphereStacks;
        patchRenderSystems.capSlices = sphereSlices;
    }
}

void rebuildPatchRenderSystems(const ParticleSystem &particleSystem,
                               PatchRenderSystems &patchRenderSystems)
{
    if (!particleSystem.hasPatchyMetadata() || !patchRenderSystems.coneSystem)
    {
        return;
    }

    patchRenderSystems.coneSystem->clear();
    for (PatchCapSystem &capSystem : patchRenderSystems.capSystems)
    {
        capSystem.system->clear();
    }

    const std::vector<Particle> &particles = particleSystem.particles();
    const std::vector<PatchyParticleData> &patchMetadata = particleSystem.patchyMetadata();
    for (size_t particleIndex = 0; particleIndex < particles.size(); ++particleIndex)
    {
        const Particle &particle = particles[particleIndex];
        if (!particle.visible)
        {
            continue;
        }

        const PatchyParticleData &patchData = patchMetadata[particleIndex];
        const std::vector<bx::Vec3> &referenceDirections =
            patchPlacementDirections(patchData.bondIds.size(), patchData.planarPlacement);
        const float clampedCosHalfAngle = std::clamp(patchData.cosHalfAngle, 0.0f, 1.0f);
        const float sinHalfAngle = std::sqrt(std::max(0.0f,
                                                      1.0f - clampedCosHalfAngle
                                                               * clampedCosHalfAngle));
        const float coneRadius = patchData.capRadius * sinHalfAngle;
        const float coneHeight = patchData.capRadius * clampedCosHalfAngle;
        PatchCapSystem *capSystem = findPatchCapSystem(patchRenderSystems,
                                                       patchData.cosHalfAngle);
        if (capSystem == nullptr)
        {
            continue;
        }

        for (const bx::Vec3 &referenceDirection : referenceDirections)
        {
            const bx::Vec3 rotatedDirection =
                rotatePatchDirection(patchData.orientationMatrix, referenceDirection);

            Particle coneParticle;
            coneParticle.id = particle.id;
            coneParticle.position = particle.position;
            coneParticle.direction = rotatedDirection;
            coneParticle.baseColor = kPatchColor;
            coneParticle.color = kPatchColor;
            coneParticle.visible = true;
            coneParticle.sizeParams[0] = coneRadius;
            coneParticle.sizeParams[1] = coneHeight;
            patchRenderSystems.coneSystem->addParticle(coneParticle);

            Particle capParticle;
            capParticle.id = particle.id;
            capParticle.position = particle.position;
            capParticle.direction = rotatedDirection;
            capParticle.baseColor = kPatchColor;
            capParticle.color = kPatchColor;
            capParticle.visible = true;
            capParticle.sizeParams[0] = patchData.capRadius;
            capSystem->system->addParticle(capParticle);
        }
    }
}

void renderPatchRenderSystems(PatchRenderSystems &patchRenderSystems, bgfx::ViewId viewId,
                              bgfx::ProgramHandle program, const float *parentTransform,
                              uint64_t renderState, const bx::Vec3 &positionOffset,
                              float particleSizeScale, const SimulationBox *simulationBox,
                              bool wrapToPeriodicBox,
                              const std::unordered_set<uint32_t> *selectedParticleIds,
                              bool usePickColors, bool cutPlaneEnabled,
                              float cutPlaneSceneZ)
{
    if (patchRenderSystems.coneSystem)
    {
        patchRenderSystems.coneSystem->render(viewId, program, parentTransform, renderState,
                                              positionOffset, particleSizeScale,
                                              simulationBox, wrapToPeriodicBox,
                                              selectedParticleIds, nullptr, usePickColors,
                                              cutPlaneEnabled, cutPlaneSceneZ);
    }

    for (PatchCapSystem &capSystem : patchRenderSystems.capSystems)
    {
        capSystem.system->render(viewId, program, parentTransform, renderState,
                                 positionOffset, particleSizeScale,
                                 simulationBox, wrapToPeriodicBox,
                                 selectedParticleIds, nullptr, usePickColors,
                                 cutPlaneEnabled, cutPlaneSceneZ);
    }
}

const Particle *resolveBondTarget(const ParticleSystem &particleSystem, int32_t bondId)
{
    if (bondId < 0)
    {
        return nullptr;
    }

    const std::vector<Particle> &particles = particleSystem.particles();
    const size_t zeroBasedIndex = static_cast<size_t>(bondId);
    if (zeroBasedIndex < particles.size())
    {
        return &particles[zeroBasedIndex];
    }

    const auto particleIt = std::find_if(
        particles.begin(), particles.end(),
        [bondId](const Particle &particle) {
            return particle.id == static_cast<uint32_t>(bondId + 1);
        });
    if (particleIt != particles.end())
    {
        return &(*particleIt);
    }

    return nullptr;
}

void selectBondedNeighbors(const ParticleSystem &particleSystem,
                           std::unordered_set<uint32_t> &selectedIds)
{
    if (!particleSystem.hasPatchyMetadata() || selectedIds.empty())
    {
        return;
    }

    const std::unordered_set<uint32_t> originalSelection = selectedIds;
    const std::vector<Particle> &particles = particleSystem.particles();
    const std::vector<PatchyParticleData> &patchMetadata = particleSystem.patchyMetadata();
    for (size_t particleIndex = 0; particleIndex < particles.size(); ++particleIndex)
    {
        const Particle &particle = particles[particleIndex];
        const bool sourceWasSelected = originalSelection.contains(particle.id);

        for (int32_t bondId : patchMetadata[particleIndex].bondIds)
        {
            const Particle *bondTarget = resolveBondTarget(particleSystem, bondId);
            if (bondTarget == nullptr)
            {
                continue;
            }

            if (sourceWasSelected)
            {
                selectedIds.insert(bondTarget->id);
            }

            if (originalSelection.contains(bondTarget->id))
            {
                selectedIds.insert(particle.id);
            }
        }
    }
}

void ensureBondRenderSystems(const bgfx::VertexLayout &layout, uint16_t sphereStacks,
                             uint16_t sphereSlices, const ParticleSystem &particleSystem,
                             BondRenderSystems &bondRenderSystems)
{
    if (!particleSystem.hasPatchyMetadata())
    {
        bondRenderSystems.cylinderSystem.reset();
        bondRenderSystems.nodeSystem.reset();
        return;
    }

    if (!bondRenderSystems.cylinderSystem || bondRenderSystems.cylinderSlices != sphereSlices)
    {
        bondRenderSystems.cylinderSystem = std::make_unique<ParticleSystem>(
            createCylinderParticleType(layout, sphereSlices));
        bondRenderSystems.cylinderSlices = sphereSlices;
    }

    if (!bondRenderSystems.nodeSystem || bondRenderSystems.sphereStacks != sphereStacks
        || bondRenderSystems.sphereSlices != sphereSlices)
    {
        bondRenderSystems.nodeSystem = std::make_unique<ParticleSystem>(
            createSphereParticleType(layout, sphereStacks, sphereSlices));
        bondRenderSystems.sphereStacks = sphereStacks;
        bondRenderSystems.sphereSlices = sphereSlices;
    }
}

void rebuildBondRenderSystems(const ParticleSystem &particleSystem,
                              const SimulationBox &simulationBox,
                              const bx::Vec3 &positionOffset,
                              bool wrapToPeriodicBox,
                              BondRenderSystems &bondRenderSystems)
{
    if (!particleSystem.hasPatchyMetadata() || !bondRenderSystems.cylinderSystem
        || !bondRenderSystems.nodeSystem)
    {
        return;
    }

    bondRenderSystems.cylinderSystem->clear();
    bondRenderSystems.nodeSystem->clear();

    const auto displayedPositionFor = [&](const bx::Vec3 &rawPosition) {
        bx::Vec3 displayedPosition = {rawPosition.x + positionOffset.x,
                                      rawPosition.y + positionOffset.y,
                                      rawPosition.z + positionOffset.z};
        if (wrapToPeriodicBox)
        {
            simulationBox.wrapPosition(displayedPosition);
        }
        return displayedPosition;
    };

    const std::vector<Particle> &particles = particleSystem.particles();
    const std::vector<PatchyParticleData> &patchMetadata = particleSystem.patchyMetadata();
    for (size_t particleIndex = 0; particleIndex < particles.size(); ++particleIndex)
    {
        const Particle &particle = particles[particleIndex];
        if (!particle.visible)
        {
            continue;
        }

        const bx::Vec3 displayedSourcePosition = displayedPositionFor(particle.position);

        Particle nodeParticle;
        nodeParticle.id = particle.id;
        nodeParticle.position = displayedSourcePosition;
        nodeParticle.baseColor = particle.baseColor;
        nodeParticle.color = particle.color;
        nodeParticle.visible = true;
        nodeParticle.sizeParams[0] = 0.25f * particleRadius(particle);
        nodeParticle.sizeParams[1] = nodeParticle.sizeParams[0];
        nodeParticle.sizeParams[2] = nodeParticle.sizeParams[0];
        bondRenderSystems.nodeSystem->addParticle(nodeParticle);

        for (int32_t bondId : patchMetadata[particleIndex].bondIds)
        {
            const Particle *bondTarget = resolveBondTarget(particleSystem, bondId);
            if (bondTarget == nullptr || !bondTarget->visible)
            {
                continue;
            }

            bx::Vec3 displacement = {
                bondTarget->position.x - particle.position.x,
                bondTarget->position.y - particle.position.y,
                bondTarget->position.z - particle.position.z,
            };
            displacement = simulationBox.nearestImage(displacement);

            const bx::Vec3 halfDisplacement = bx::mul(displacement, 0.5f);
            const float cylinderLength = bx::length(halfDisplacement);
            if (cylinderLength <= 1.0e-6f)
            {
                continue;
            }

            Particle cylinderParticle;
            cylinderParticle.id = particle.id;
            cylinderParticle.position = {
                displayedSourcePosition.x + 0.25f * displacement.x,
                displayedSourcePosition.y + 0.25f * displacement.y,
                displayedSourcePosition.z + 0.25f * displacement.z,
            };
            cylinderParticle.direction = halfDisplacement;
            cylinderParticle.baseColor = particle.baseColor;
            cylinderParticle.color = particle.color;
            cylinderParticle.visible = true;
            cylinderParticle.sizeParams[0] = 0.125f * particleRadius(particle);
            cylinderParticle.sizeParams[1] = cylinderLength;
            bondRenderSystems.cylinderSystem->addParticle(cylinderParticle);
        }
    }
}

void renderBondRenderSystems(BondRenderSystems &bondRenderSystems, bgfx::ViewId viewId,
                             bgfx::ProgramHandle program, const float *parentTransform,
                             uint64_t renderState, float particleSizeScale,
                             const std::unordered_set<uint32_t> *selectedParticleIds,
                             bool usePickColors, bool cutPlaneEnabled,
                             float cutPlaneSceneZ)
{
    const bx::Vec3 zeroOffset{0.0f, 0.0f, 0.0f};

    if (bondRenderSystems.cylinderSystem)
    {
        bondRenderSystems.cylinderSystem->render(viewId, program, parentTransform, renderState,
                                                 zeroOffset, 1.0f,
                                                 nullptr, false,
                                                 selectedParticleIds, nullptr, usePickColors,
                                                 cutPlaneEnabled, cutPlaneSceneZ);
    }
    if (bondRenderSystems.nodeSystem)
    {
        bondRenderSystems.nodeSystem->render(viewId, program, parentTransform, renderState,
                                             zeroOffset, particleSizeScale,
                                             nullptr, false,
                                             selectedParticleIds, nullptr, usePickColors,
                                             cutPlaneEnabled, cutPlaneSceneZ);
    }
}

PolygonRenderSystem *findPolygonRenderSystem(PolygonRenderSystems &polygonRenderSystems,
                                             uint16_t sideCount)
{
    for (PolygonRenderSystem &polygonRenderSystem : polygonRenderSystems.systems)
    {
        if (polygonRenderSystem.sideCount == sideCount)
        {
            return &polygonRenderSystem;
        }
    }

    return nullptr;
}

void ensurePolygonRenderSystems(const bgfx::VertexLayout &layout,
                                const ParticleSystem &particleSystem,
                                PolygonRenderSystems &polygonRenderSystems)
{
    std::vector<uint16_t> uniqueSideCounts;
    for (const Particle &particle : particleSystem.particles())
    {
        const uint16_t sideCount = polygonSideCount(particle);
        if (sideCount < 3u)
        {
            continue;
        }

        if (std::find(uniqueSideCounts.begin(), uniqueSideCounts.end(), sideCount)
            == uniqueSideCounts.end())
        {
            uniqueSideCounts.push_back(sideCount);
        }
    }
    std::sort(uniqueSideCounts.begin(), uniqueSideCounts.end());

    bool mustRebuildSystems = polygonRenderSystems.systems.size() != uniqueSideCounts.size();
    if (!mustRebuildSystems)
    {
        for (size_t index = 0; index < uniqueSideCounts.size(); ++index)
        {
            if (polygonRenderSystems.systems[index].sideCount != uniqueSideCounts[index])
            {
                mustRebuildSystems = true;
                break;
            }
        }
    }

    if (mustRebuildSystems)
    {
        polygonRenderSystems.systems.clear();
        polygonRenderSystems.systems.reserve(uniqueSideCounts.size());
        for (uint16_t sideCount : uniqueSideCounts)
        {
            polygonRenderSystems.systems.push_back(PolygonRenderSystem{
                .sideCount = sideCount,
                .system = std::make_unique<ParticleSystem>(
                    createPolygonParticleType(layout, sideCount)),
            });
        }
    }
}

void rebuildPolygonRenderSystems(const ParticleSystem &particleSystem,
                                 PolygonRenderSystems &polygonRenderSystems)
{
    for (PolygonRenderSystem &polygonRenderSystem : polygonRenderSystems.systems)
    {
        polygonRenderSystem.system->clear();
    }

    for (const Particle &particle : particleSystem.particles())
    {
        if (!particle.visible)
        {
            continue;
        }

        PolygonRenderSystem *polygonRenderSystem =
            findPolygonRenderSystem(polygonRenderSystems, polygonSideCount(particle));
        if (polygonRenderSystem == nullptr)
        {
            continue;
        }

        polygonRenderSystem->system->addParticle(particle);
    }
}

void renderPolygonRenderSystems(PolygonRenderSystems &polygonRenderSystems,
                                bgfx::ViewId viewId, bgfx::ProgramHandle program,
                                const float *parentTransform, uint64_t renderState,
                                const bx::Vec3 &positionOffset, float particleSizeScale,
                                const SimulationBox *simulationBox,
                                bool wrapToPeriodicBox,
                                const std::unordered_set<uint32_t> *selectedParticleIds,
                                bool usePickColors, bool cutPlaneEnabled,
                                float cutPlaneSceneZ)
{
    for (PolygonRenderSystem &polygonRenderSystem : polygonRenderSystems.systems)
    {
        polygonRenderSystem.system->render(viewId, program, parentTransform, renderState,
                                           positionOffset, particleSizeScale,
                                           simulationBox, wrapToPeriodicBox,
                                           selectedParticleIds, nullptr, usePickColors,
                                           cutPlaneEnabled, cutPlaneSceneZ);
    }
}

void rebuildMobilitySystem(const ParticleSystem &particleSystem,
                           ParticleSystem &mobilitySystem,
                           const ViewerState &viewerState,
                           const SimulationBox &simulationBox)
{
    mobilitySystem.clear();

    const std::vector<Particle> &particles = particleSystem.particles();
    if (!viewerState.hasPreviousFramePositions
        || viewerState.previousRawPositions.size() != particles.size())
    {
        return;
    }

    mobilitySystem.reserve(particles.size());
    for (size_t index = 0; index < particles.size(); ++index)
    {
        const Particle &particle = particles[index];
        if (!particle.visible)
        {
            continue;
        }

        const bx::Vec3 previousPosition = viewerState.previousRawPositions[index];
        const bx::Vec3 rawDelta = {particle.position.x - previousPosition.x,
                                   particle.position.y - previousPosition.y,
                                   particle.position.z - previousPosition.z};
        const bx::Vec3 displacement = simulationBox.nearestImage(rawDelta);
        const float displacementLength = bx::length(displacement);
        if (displacementLength <= 1.0e-6f)
        {
            continue;
        }

        bx::Vec3 displayPreviousPosition = previousPosition;
        simulationBox.wrapPosition(displayPreviousPosition);

        const float diameter = 2.0f * particleRadius(particle);
        const float desiredTipLength = 0.5f * diameter;
        const float desiredTipRadius = 0.25f * diameter;
        const float shaftRadius = 0.125f * diameter;
        const float tipLength = bx::min(desiredTipLength, displacementLength);
        const float shaftLength = bx::max(0.0f, displacementLength - tipLength);
        const float tipScale = desiredTipLength > 1.0e-6f ? tipLength / desiredTipLength : 1.0f;

        Particle arrow;
        arrow.id = particle.id;
        arrow.position = displayPreviousPosition;
        arrow.direction = displacement;
        arrow.baseColor = particle.baseColor;
        arrow.color = arrow.baseColor;
        arrow.visible = true;
        arrow.sizeParams[0] = shaftRadius;
        arrow.sizeParams[1] = shaftLength;
        arrow.sizeParams[2] = desiredTipRadius * tipScale;
        arrow.sizeParams[3] = tipLength;
        mobilitySystem.addParticle(arrow);
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
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
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
            markPickBufferDirty(*state);
            break;
        case GLFW_KEY_LEFT:
            if ((mods & GLFW_MOD_ALT) != 0)
            {
                applySceneRotation(*state, 0.0f, 0.5f * bx::kPi, 0.0f);
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
            if ((mods & GLFW_MOD_ALT) != 0)
            {
                applySceneRotation(*state, 0.0f, -0.5f * bx::kPi, 0.0f);
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
            if ((mods & GLFW_MOD_ALT) != 0)
            {
                applySceneRotation(*state, 0.5f * bx::kPi, 0.0f, 0.0f);
                markPickBufferDirty(*state);
            }
            break;
        case GLFW_KEY_DOWN:
            if ((mods & GLFW_MOD_ALT) != 0)
            {
                applySceneRotation(*state, -0.5f * bx::kPi, 0.0f, 0.0f);
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
                    }
                    markPickBufferDirty(*state);
                }
                else
                {
                    state->showBox = !state->showBox;
                }
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
                }
                markPickBufferDirty(*state);
            }
            else if (action == GLFW_PRESS)
            {
                const uint8_t nextMode =
                    (static_cast<uint8_t>(state->colorMode) + 1u)
                    % static_cast<uint8_t>(ColorMode::Count);
                state->colorMode = static_cast<ColorMode>(nextMode);
            }
            break;
        case GLFW_KEY_S:
            if (action == GLFW_PRESS)
            {
                state->lightingLevelIndex =
                    static_cast<uint8_t>((state->lightingLevelIndex + 1u) % kLightingLevelCount);
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
        case GLFW_KEY_KP_ADD:
            if (action == GLFW_PRESS)
            {
                state->pendingIncreaseSphereResolution = true;
            }
            break;
        case GLFW_KEY_PERIOD:
            if (action == GLFW_PRESS)
            {
                if ((mods & GLFW_MOD_SHIFT) != 0)
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
            if (action == GLFW_PRESS)
            {
                if ((mods & GLFW_MOD_SHIFT) != 0)
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

            state->leftMouseDown = false;
            state->leftDragActive = false;
            state->leftTranslateMode = false;
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
            state->rightMouseDown = false;
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
        std::cerr << "GPU picking disabled after resize: "
                  << pickResources.disableReason << std::endl;
    }
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
        noteEncounteredParticleTypes(viewerState, particleSystem);
        applyParticleVisibilityFilters(particleSystem, viewerState);
        markPickBufferDirty(viewerState);
        return;
    }

    std::cerr << "Failed to load frame " << requestedFrame
              << " from trajectory file: " << loadedPath;
    if (trajectoryReader != nullptr && !trajectoryReader->error().empty())
    {
        std::cerr << "\n" << trajectoryReader->error();
    }
    std::cerr << std::endl;
}

static void processPendingActions(ViewerState &viewerState, ParticleSystem &particleSystem,
                                  const bgfx::VertexLayout &layout,
                                  TrajectoryReader::FileType particleFileType,
                                  const SimulationBox &simulationBox,
                                  float particleSizeScale,
                                  uint16_t &sphereStacks, uint16_t &sphereSlices,
                                  float cutPlaneStep, float cutPlaneMinSceneZ,
                                  float cutPlaneMaxSceneZ, int &exitCode)
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
            markPickBufferDirty(viewerState);
        }
        viewerState.pendingHideSelected = false;
        viewerState.lastPickedId = 0;
        viewerState.pendingPickRequest = false;
    }

    if (viewerState.pendingRevealAll)
    {
        const bool hiddenChanged = revealAllParticles(particleSystem, viewerState.hiddenIds);
        const bool visibilityChanged = applyParticleVisibilityFilters(particleSystem,
                                                                      viewerState);
        if (hiddenChanged || visibilityChanged)
        {
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

    if (viewerState.pendingDescribeSelection)
    {
        printSelectedParticles(particleSystem, viewerState.selectedIds, simulationBox);
        viewerState.pendingDescribeSelection = false;
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

    const bool supportsResolutionAdjustment = particleFileType != TrajectoryReader::FileType::Polygon;
    bool changedSphereResolution = false;
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

    if (changedSphereResolution)
    {
        particleSystem.setType(createParticleType(layout, particleFileType,
                                                  sphereStacks, sphereSlices));
        if (!hasValidParticleMeshes(particleSystem))
        {
            std::cerr << "Failed to rebuild " << particleTypeName(particleFileType)
                      << " mesh for resolution "
                      << sphereStacks << "x" << sphereSlices << std::endl;
            exitCode = 1;
        }
        markPickBufferDirty(viewerState);
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
        bgfx::dbgTextPrintf(0, 2, 0x0f, "Selected: %d  Last pick: %u  %s: %ux%u",
                            (int)viewerState.selectedIds.size(), viewerState.lastPickedId,
                            particleTypeName(particleFileType), sphereStacks, sphereSlices);
        bgfx::dbgTextPrintf(0, 3, 0x0f,
                            "Drag rotates. Shift+drag translates. D prints selection. V prints visible count.");
    }
    else
    {
        bgfx::dbgTextPrintf(0, 2, 0x0f, "Selected: %d  Last pick: %u  %s: %ux%u",
                            (int)viewerState.selectedIds.size(), viewerState.lastPickedId,
                            particleTypeName(particleFileType), sphereStacks, sphereSlices);
        bgfx::dbgTextPrintf(0, 3, 0x0f,
                            "Drag rotates. Shift+drag translates. D prints selection. V prints visible count.");
    }
    bgfx::dbgTextPrintf(0, 4, 0x0f,
                        "Enter resets rotation. B toggles box. Shift+B toggles bonds. P saves PNG.");
    bgfx::dbgTextPrintf(0, 5, 0x0f,
                        "M cycles colors. Shift+M toggles mobility. Box: %s  Mobility: %s  Bonds: %s",
                        viewerState.showBox ? "on" : "off",
                        viewerState.mobilityModeEnabled ? "on" : "off",
                        viewerState.bondModeEnabled ? "on" : "off");
    bgfx::dbgTextPrintf(0, 6, 0x0f,
                        "Cut plane: %s  z=%.2f  Color: %s  Size: %.2f  Light: %.2f",
                        viewerState.cutPlaneEnabled ? "active" : "off",
                        viewerState.cutPlaneSceneZ,
                        colorModeName(viewerState.colorMode),
                        viewerState.particleSizeScale,
                        lightingScaleFromIndex(viewerState.lightingLevelIndex));
    bgfx::dbgTextPrintf(0, 7, 0x0f, "GPU picking: %s",
                        pickResources.enabled
                            ? (viewerState.pendingPickReadback
                                   ? "updating cache"
                                   : (hasValidPickBuffer(viewerState) ? "cached" : "stale"))
                            : pickResources.disableReason.c_str());
    if (!loadedPath.empty())
    {
        bgfx::dbgTextPrintf(
            0, 8, 0x0f,
            "Left/Right step frames. Shift+Left jumps first. Shift+Right jumps last.");
        bgfx::dbgTextPrintf(0, 9, 0x0f, "Loaded %s (frame %d/%d)", loadedPath.c_str(),
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

    // Create a GLFW window without an OpenGL context.
    glfwSetErrorCallback(glfw_errorCallback);
    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(1536, 768, "Colloid Visualization Tool", nullptr, nullptr);
    if (!window)
        return 1;
    glfwSetWindowUserPointer(window, &viewerState);
    glfwSetKeyCallback(window, glfw_keyCallback);
    glfwSetCharCallback(window, glfw_charCallback);
    glfwSetCursorPosCallback(window, glfw_mouseCallback);
    glfwSetMouseButtonCallback(window, glfw_mouseButtonCallback);
    glfwSetScrollCallback(window, glfw_scrollCallback);
    // Call bgfx::renderFrame before bgfx::init to signal to bgfx not to create a render thread.
    // Most graphics APIs must be used on the same thread that created the window.
    bgfx::renderFrame();
    // Initialize bgfx using the native window handle and window resolution.
    bgfx::Init init;
#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
    init.platformData.ndt = glfwGetX11Display();
    init.platformData.nwh = (void *)(uintptr_t)glfwGetX11Window(window);
#elif BX_PLATFORM_OSX
    init.platformData.nwh = glfwGetCocoaWindow(window);
#elif BX_PLATFORM_WINDOWS
    init.platformData.nwh = glfwGetWin32Window(window);
#endif
    int width, height;
    glfwGetWindowSize(window, &width, &height);
    init.type = bgfx::RendererType::OpenGL;
    init.callback = &screenshotCallback;
    init.resolution.width = (uint32_t)width;
    init.resolution.height = (uint32_t)height;
    init.resolution.reset = BGFX_RESET_VSYNC;
    if (!bgfx::init(init))
        return 1;

    {
        // Vertex layout
        bgfx::VertexLayout layout;
        bgfx::VertexLayout lineLayout;
        PosNormalVertex::init(layout);
        LineVertex::init(lineLayout);
        uint16_t sphereStacks = kDefaultSphereStacks;
        uint16_t sphereSlices = kDefaultSphereSlices;

        SimulationBox simulationBox({-7.5f, -7.5f, -7.5f}, {7.5f, 7.5f, 7.5f});
        simulationBox.setPeriodic(true, true, true);

        // Load particle systems. Additional particle families can add more systems here.
        ParticleSystem particleSystem(
            createSphereParticleType(layout, sphereStacks, sphereSlices));
        ParticleSystem mobilitySystem(createArrowParticleType(layout, kDefaultArrowSlices));
        PatchRenderSystems patchRenderSystems;
        BondRenderSystems bondRenderSystems;
        PolygonRenderSystems polygonRenderSystems;
        TrajectoryReader::FileType particleFileType = TrajectoryReader::FileType::Sphere;

        // Load shaders
        auto vs = loadShader("shaders/vs_instancing.bin");
        auto fs = loadShader("shaders/fs_instancing.bin");
        auto program = bgfx::createProgram(vs, fs, true);
        auto pickVs = loadShader("shaders/vs_picking.bin");
        auto pickFs = loadShader("shaders/fs_picking.bin");
        auto pickProgram = bgfx::createProgram(pickVs, pickFs, true);
        auto lineVs = loadShader("shaders/vs_lines.bin");
        auto lineFs = loadShader("shaders/fs_lines.bin");
        auto lineProgram = bgfx::createProgram(lineVs, lineFs, true);
        // Create light direction and brightness uniforms.
        bgfx::UniformHandle u_lightDir =
            bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
        bgfx::UniformHandle u_lightScale =
            bgfx::createUniform("u_lightScale", bgfx::UniformType::Vec4);
        PickResources pickResources;
        size_t currentFrame = 0;
        size_t totalFrames = 1;
        std::string loadedPath;
        std::unique_ptr<TrajectoryReader> trajectoryReader;

        if (argc > 1)
        {
            loadedPath = argv[1];
            trajectoryReader = std::make_unique<TrajectoryReader>(loadedPath);
            if (!trajectoryReader->isOpen())
            {
                std::cerr << trajectoryReader->error() << std::endl;
                exitCode = 1;
            }
            else
            {
                particleFileType = trajectoryReader->fileType();
                viewerState.fileDimensionality = trajectoryReader->dimensionality();
                particleSystem.setType(createParticleType(layout, particleFileType,
                                                          sphereStacks, sphereSlices));
                totalFrames = trajectoryReader->frameCount();
                if (!trajectoryReader->loadFrame(currentFrame, particleSystem, simulationBox))
                {
                    std::cerr << "Failed to load frame 0 from trajectory file: "
                              << loadedPath;
                    if (!trajectoryReader->error().empty())
                    {
                        std::cerr << "\n" << trajectoryReader->error();
                    }
                    std::cerr << std::endl;
                    exitCode = 1;
                }
                else
                {
                    noteEncounteredParticleTypes(viewerState, particleSystem);
                    applyParticleVisibilityFilters(particleSystem, viewerState);
                    snapshotCurrentParticlePositions(particleSystem, viewerState, false);
                }
            }
        }
        else
        {
            std::cerr << "No filename given." << std::endl;
            exitCode = 1;
        }

        bgfx::setViewClear(kMainView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xffffffff, 1.0f, 0);
        bgfx::setViewClear(kPickView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x00000000, 1.0f, 0);
        const bool uiAvailable = ImGuiBgfx::create(kUiView);
        if (!uiAvailable)
        {
            std::cerr << "Dear ImGui UI unavailable; continuing without UI." << std::endl;
        }
        viewerState.uiPanelWidth = computeUiPanelWidth(static_cast<uint16_t>(width), viewerState.showUi);
        viewerState.renderViewportWidth = computeRenderViewportWidth(static_cast<uint16_t>(width),
                                                                     viewerState.showUi);
        viewerState.renderViewportHeight = static_cast<uint16_t>(bx::max(height, 1));

        float initialAspectRatio = height > 0
                                       ? float(viewerState.renderViewportWidth) / float(height)
                                       : 1.0f;
        float cameraDistance = computeInitialCameraDistance(simulationBox);
        bx::Vec3 cameraPos = {0.0f, 0.0f, cameraDistance};
        float orthoBaseHalfHeight =
            computeInitialOrthoHalfHeight(simulationBox, initialAspectRatio);
        float nearPlane = 0.1f;
        float farPlane = computeInitialFarPlane(cameraDistance, simulationBox);
        const float cutPlaneStep = computeCutPlaneStep(simulationBox);
        const float cutPlaneMinSceneZ = -0.5f * std::sqrt(simulationBox.size().x * simulationBox.size().x
                                                          + simulationBox.size().y * simulationBox.size().y
                                                          + simulationBox.size().z * simulationBox.size().z);
        const float cutPlaneMaxSceneZ = cameraDistance;

        bgfx::setViewRect(kMainView, 0, 0, viewerState.renderViewportWidth,
                          viewerState.renderViewportHeight);
        if (!createPickResources(pickResources, viewerState.renderViewportWidth,
                                 viewerState.renderViewportHeight))
        {
            std::cerr << "GPU picking disabled: " << pickResources.disableReason << std::endl;
        }

        if (exitCode == 0 && (!hasValidParticleMeshes(particleSystem) || !bgfx::isValid(program)))
        {
            fprintf(stderr, "INVALID BGFX RESOURCE: particleType=%d program=%d\\n",
                hasValidParticleMeshes(particleSystem),
                    bgfx::isValid(program));
            exitCode = 1;
        }
        if (exitCode == 0 && !bgfx::isValid(lineProgram))
        {
            std::cerr << "Wireframe box shaders unavailable" << std::endl;
            exitCode = 1;
        }
        if (exitCode == 0 && !bgfx::isValid(pickProgram))
        {
            pickResources.enabled = false;
            if (pickResources.disableReason.empty())
            {
                pickResources.disableReason = "pick shaders unavailable";
            }
        }

        uint32_t lastSubmittedFrame = 0;
    double lastUiFrameTime = glfwGetTime();

        while (exitCode == 0 && !glfwWindowShouldClose(window))
        {
            glfwPollEvents();

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

            handleWindowResize(window, width, height, viewerState, pickResources);
            handleTrajectoryFrameChange(viewerState, currentFrame, totalFrames, loadedPath,
                                        trajectoryReader.get(), particleSystem, simulationBox);
            processPendingActions(viewerState, particleSystem, layout, particleFileType,
                                  simulationBox,
                                  viewerState.particleSizeScale,
                                  sphereStacks,
                                  sphereSlices, cutPlaneStep, cutPlaneMinSceneZ,
                                  cutPlaneMaxSceneZ, exitCode);

            // Fixed camera and light
            float view[16];
            float proj[16];
            bx::mtxLookAt(view, cameraPos, s_cameraTarget, bx::Vec3{0.0f, 1.0f, 0.0f}, bx::Handedness::Right);
            bgfx::setViewRect(kMainView, 0, 0, viewerState.renderViewportWidth,
                              viewerState.renderViewportHeight);
            float currentAspectRatio = height > 0
                                           ? float(viewerState.renderViewportWidth) / float(height)
                                           : 1.0f;
            float zoomedHalfHeight = orthoBaseHalfHeight * viewerState.orthoZoom;
            float zoomedHalfWidth = currentAspectRatio * zoomedHalfHeight;
            updateMouseDrivenInteraction(viewerState, viewerState.renderViewportWidth,
                                         viewerState.renderViewportHeight,
                                         zoomedHalfWidth, zoomedHalfHeight);
            bx::mtxOrtho(proj, -zoomedHalfWidth, zoomedHalfWidth, -zoomedHalfHeight,
                         zoomedHalfHeight, nearPlane, farPlane, 0.0f,
                         bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
            bgfx::setViewTransform(kMainView, view, proj);

            const double currentUiFrameTime = glfwGetTime();
            const float uiDeltaTime = static_cast<float>(currentUiFrameTime - lastUiFrameTime);
            lastUiFrameTime = currentUiFrameTime;
            ImGuiBgfx::beginFrame(window, static_cast<uint16_t>(width),
                                  static_cast<uint16_t>(height),
                                  viewerState.mouseX, viewerState.mouseY,
                                  uiDeltaTime, s_uiScrollX, s_uiScrollY);
            s_uiScrollX = 0.0f;
            s_uiScrollY = 0.0f;

            drawViewerControls(viewerState, particleSystem, particleFileType,
                               loadedPath, currentFrame, totalFrames,
                               static_cast<uint16_t>(width), static_cast<uint16_t>(height),
                               cutPlaneMinSceneZ, cutPlaneMaxSceneZ);

            // Set fixed world-space light direction from the shaded point toward the light.
            s_lightDir = bx::normalize(s_lightDir);
            float lightDir[4] = {s_lightDir.x, s_lightDir.y, s_lightDir.z, 1.0f};
            bgfx::setUniform(u_lightDir, lightDir, 1);
            const float lightingScale = lightingScaleFromIndex(viewerState.lightingLevelIndex);
            float lightScale[4] = {lightingScale, 0.0f, 0.0f, 0.0f};
            bgfx::setUniform(u_lightScale, lightScale, 1);

            // Build scene transform from accumulated mouse input and box centering.
            bx::Vec3 boxCenter = simulationBox.center();
            float sceneTranslateMtx[16];
            bx::mtxTranslate(sceneTranslateMtx, -boxCenter.x, -boxCenter.y, -boxCenter.z);

            float sceneTransform[16];
            bx::mtxMul(sceneTransform, sceneTranslateMtx, viewerState.sceneRotation);

            applyColorMode(particleSystem, viewerState.colorMode,
                           particleFileType == TrajectoryReader::FileType::Rod, false);
            if (isPolygonFileType(particleFileType))
            {
                ensurePolygonRenderSystems(layout, particleSystem, polygonRenderSystems);
                rebuildPolygonRenderSystems(particleSystem, polygonRenderSystems);
            }
            if (isPatchyFileType(particleFileType))
            {
                ensurePatchRenderSystems(layout, sphereStacks, sphereSlices, particleSystem,
                                         patchRenderSystems);
                rebuildPatchRenderSystems(particleSystem, patchRenderSystems);
                ensureBondRenderSystems(layout, sphereStacks, sphereSlices, particleSystem,
                                        bondRenderSystems);
                rebuildBondRenderSystems(particleSystem, simulationBox,
                                         viewerState.particleTranslation,
                                         viewerState.wrapParticlesToBox,
                                         bondRenderSystems);
            }

            if (viewerState.bondModeEnabled)
            {
                renderBondRenderSystems(bondRenderSystems, kMainView, program,
                                        sceneTransform, kParticleRenderState,
                                        viewerState.particleSizeScale,
                                        &viewerState.selectedIds, false,
                                        viewerState.cutPlaneEnabled,
                                        viewerState.cutPlaneSceneZ);
            }
            else if (viewerState.mobilityModeEnabled)
            {
                rebuildMobilitySystem(particleSystem, mobilitySystem, viewerState, simulationBox);
                applyColorMode(mobilitySystem, viewerState.colorMode, true, true);
                mobilitySystem.render(kMainView, program, sceneTransform,
                                      kParticleRenderState,
                                      viewerState.particleTranslation, 1.0f,
                                      &simulationBox,
                                      viewerState.wrapParticlesToBox,
                                      &viewerState.selectedIds, nullptr,
                                      false,
                                      viewerState.cutPlaneEnabled,
                                      viewerState.cutPlaneSceneZ);
            }
            else
            {
                if (isPolygonFileType(particleFileType))
                {
                    renderPolygonRenderSystems(polygonRenderSystems, kMainView, program,
                                               sceneTransform, kParticleRenderState,
                                               viewerState.particleTranslation,
                                               viewerState.particleSizeScale, &simulationBox,
                                               viewerState.wrapParticlesToBox,
                                               &viewerState.selectedIds, false,
                                               viewerState.cutPlaneEnabled,
                                               viewerState.cutPlaneSceneZ);
                }
                else
                {
                    particleSystem.render(kMainView, program, sceneTransform,
                                          kParticleRenderState,
                                          viewerState.particleTranslation,
                                          viewerState.particleSizeScale, &simulationBox,
                                          viewerState.wrapParticlesToBox,
                                          &viewerState.selectedIds, nullptr,
                                          false,
                                          viewerState.cutPlaneEnabled,
                                          viewerState.cutPlaneSceneZ);
                }
                if (isPatchyFileType(particleFileType))
                {
                    renderPatchRenderSystems(patchRenderSystems, kMainView, program,
                                             sceneTransform, kParticleRenderState,
                                             viewerState.particleTranslation,
                                             viewerState.particleSizeScale, &simulationBox,
                                             viewerState.wrapParticlesToBox,
                                             &viewerState.selectedIds, false,
                                             viewerState.cutPlaneEnabled,
                                             viewerState.cutPlaneSceneZ);
                }
            }
            if (viewerState.showBox)
            {
                renderSimulationBoxWireframe(kMainView, lineProgram, lineLayout,
                                             simulationBox, sceneTransform);
            }

            if (pickResources.enabled && viewerState.pendingPickRequest
                && hasValidPickBuffer(viewerState))
            {
                resolvePendingPickRequest(viewerState, pickResources);
            }

            const bool shouldRefreshPickBuffer =
                pickResources.enabled && bgfx::isValid(pickProgram)
                && !viewerState.pendingPickReadback && !hasValidPickBuffer(viewerState)
                && !viewerState.leftMouseDown && !viewerState.rightMouseDown;
            if (shouldRefreshPickBuffer)
            {
                bgfx::setViewRect(kPickView, 0, 0, pickResources.width, pickResources.height);
                bgfx::setViewFrameBuffer(kPickView, pickResources.frameBuffer);
                bgfx::setViewTransform(kPickView, view, proj);
                if (viewerState.bondModeEnabled)
                {
                    renderBondRenderSystems(bondRenderSystems, kPickView, pickProgram,
                                            sceneTransform, kParticleRenderState,
                                            viewerState.particleSizeScale,
                                            nullptr, true,
                                            viewerState.cutPlaneEnabled,
                                            viewerState.cutPlaneSceneZ);
                }
                else if (viewerState.mobilityModeEnabled)
                {
                    mobilitySystem.render(kPickView, pickProgram, sceneTransform,
                                          kParticleRenderState,
                                          viewerState.particleTranslation,
                                          1.0f,
                                          &simulationBox, viewerState.wrapParticlesToBox,
                                          nullptr, nullptr,
                                          true, viewerState.cutPlaneEnabled,
                                          viewerState.cutPlaneSceneZ);
                }
                else
                {
                    if (isPolygonFileType(particleFileType))
                    {
                        renderPolygonRenderSystems(polygonRenderSystems, kPickView, pickProgram,
                                                   sceneTransform, kParticleRenderState,
                                                   viewerState.particleTranslation,
                                                   viewerState.particleSizeScale,
                                                   &simulationBox,
                                                   viewerState.wrapParticlesToBox,
                                                   nullptr, true,
                                                   viewerState.cutPlaneEnabled,
                                                   viewerState.cutPlaneSceneZ);
                    }
                    else
                    {
                        particleSystem.render(kPickView, pickProgram, sceneTransform,
                                              kParticleRenderState,
                                              viewerState.particleTranslation,
                                              viewerState.particleSizeScale,
                                              &simulationBox,
                                              viewerState.wrapParticlesToBox, nullptr, nullptr,
                                              true,
                                              viewerState.cutPlaneEnabled,
                                              viewerState.cutPlaneSceneZ);
                    }
                    if (isPatchyFileType(particleFileType))
                    {
                        renderPatchRenderSystems(patchRenderSystems, kPickView, pickProgram,
                                                 sceneTransform, kParticleRenderState,
                                                 viewerState.particleTranslation,
                                                 viewerState.particleSizeScale,
                                                 &simulationBox,
                                                 viewerState.wrapParticlesToBox,
                                                 nullptr, true,
                                                 viewerState.cutPlaneEnabled,
                                                 viewerState.cutPlaneSceneZ);
                    }
                }

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
                             sphereStacks,
                             sphereSlices, loadedPath, currentFrame, totalFrames);
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
        ImGuiBgfx::destroy();
        if (bgfx::isValid(pickProgram))
        {
            bgfx::destroy(pickProgram);
        }
        if (bgfx::isValid(lineProgram))
        {
            bgfx::destroy(lineProgram);
        }
        if (bgfx::isValid(program))
        {
            bgfx::destroy(program);
        }
        if (bgfx::isValid(u_lightDir))
        {
            bgfx::destroy(u_lightDir);
        }
        if (bgfx::isValid(u_lightScale))
        {
            bgfx::destroy(u_lightScale);
        }
    }

    bgfx::shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}