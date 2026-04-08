#include "AppBootstrap.h"
#include "ArrowType.h"
#include "ColorPalette.h"
#include "CubeType.h"
#include "CylinderType.h"
#include "ImGuiBgfx.h"
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

const char *shaderOutputDirectoryForRenderer(bgfx::RendererType::Enum rendererType)
{
    switch (rendererType)
    {
    case bgfx::RendererType::Direct3D11:
        return "dxbc";
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
            std::cout << "bgfx renderer backend unavailable: "
                      << bgfx::getRendererName(rendererType) << std::endl;
            continue;
        }

        init.type = rendererType;
        if (bgfx::init(init))
        {
            std::cout << "Using bgfx renderer backend: "
                      << bgfx::getRendererName(bgfx::getRendererType()) << std::endl;
            return true;
        }

        std::cerr << "Failed to initialize bgfx renderer backend: "
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
constexpr bgfx::ViewId kStructureFactorView = 5;
constexpr bgfx::ViewId kStructureFactorColorView = 6;
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
constexpr uint32_t kMaxNeighborsPerParticle = 50u;
constexpr uint16_t kBondDiagramPreviewSize = 192u;
constexpr uint16_t kInteractiveStructureFactorLowResSize = 128u;
constexpr uint16_t kBondDiagramSphereStacks = 10u;
constexpr uint16_t kBondDiagramSphereSlices = 10u;
float s_uiScrollX = 0.0f;
float s_uiScrollY = 0.0f;

struct NeighborCellAddress
{
    int x = 0;
    int y = 0;
    int z = 0;
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

struct ScreenSpaceVertex
{
    float x;
    float y;
    float z;

    static void init(bgfx::VertexLayout &layout)
    {
        layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .end();
    }
};

bool renderStructureFactorPreviewGpu(const StructureFactorSettings &settings,
                                     const SimulationBox &simulationBox,
                                     const ParticleSystem &particleSystem,
                                     StructureFactorResources &structureFactorResources,
                                     uint32_t particleDataRevision,
                                     std::string &error);

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

std::array<float, 4> analysisGradientColor(float value)
{
    const float clampedValue = std::clamp(value, 0.0f, 1.0f);
    if (clampedValue <= 0.5f)
    {
        const float t = clampedValue / 0.5f;
        return {
            0.5f * t,
            0.5f * t,
            1.0f - 0.5f * t,
            1.0f,
        };
    }

    const float t = (clampedValue - 0.5f) / 0.5f;
    return {
        0.5f + 0.5f * t,
        0.5f - 0.5f * t,
        0.5f - 0.5f * t,
        1.0f,
    };
}

std::array<float, 4> orderParameterGradientColor(float value)
{
    const float clampedValue = std::clamp(value, 0.0f, 1.0f);
    constexpr std::array<float, 4> kLowColor = {0.0f, 0.0f, 1.0f, 1.0f};
    constexpr std::array<float, 4> kMidColor = {0.82f, 0.82f, 0.82f, 1.0f};
    constexpr std::array<float, 4> kHighColor = {1.0f, 0.0f, 0.0f, 1.0f};

    if (clampedValue <= 0.5f)
    {
        const float t = clampedValue / 0.5f;
        return {
            kLowColor[0] * (1.0f - t) + kMidColor[0] * t,
            kLowColor[1] * (1.0f - t) + kMidColor[1] * t,
            kLowColor[2] * (1.0f - t) + kMidColor[2] * t,
            1.0f,
        };
    }

    const float t = (clampedValue - 0.5f) / 0.5f;
    return {
        kMidColor[0] * (1.0f - t) + kHighColor[0] * t,
        kMidColor[1] * (1.0f - t) + kHighColor[1] * t,
        kMidColor[2] * (1.0f - t) + kHighColor[2] * t,
        1.0f,
    };
}

struct ParticleSizeColorStats
{
    float mean = 0.0f;
    float standardDeviation = 0.0f;
};

ParticleSizeColorStats computeParticleSizeColorStats(const std::vector<Particle> &particles)
{
    ParticleSizeColorStats stats;
    if (particles.empty())
    {
        return stats;
    }

    double sizeSum = 0.0;
    for (const Particle &particle : particles)
    {
        sizeSum += static_cast<double>(particle.sizeParams[0]);
    }
    stats.mean = static_cast<float>(sizeSum / static_cast<double>(particles.size()));

    double squaredDifferenceSum = 0.0;
    for (const Particle &particle : particles)
    {
        const double delta = static_cast<double>(particle.sizeParams[0])
                             - static_cast<double>(stats.mean);
        squaredDifferenceSum += delta * delta;
    }
    stats.standardDeviation = static_cast<float>(
        std::sqrt(squaredDifferenceSum / static_cast<double>(particles.size())));
    return stats;
}

float normalizedParticleSizeValue(float particleSize, const ParticleSizeColorStats &stats)
{
    if (!(stats.standardDeviation > 1.0e-6f))
    {
        return 0.5f;
    }

    const float minSize = stats.mean - 2.0f * stats.standardDeviation;
    const float maxSize = stats.mean + 2.0f * stats.standardDeviation;
    if (!(maxSize > minSize + 1.0e-6f))
    {
        return 0.5f;
    }

    return std::clamp((particleSize - minSize) / (maxSize - minSize), 0.0f, 1.0f);
}

std::array<float, 4> hueColor(float hue)
{
    const float wrappedHue = hue - std::floor(hue);
    const float scaledHue = wrappedHue * 6.0f;
    const int sector = static_cast<int>(std::floor(scaledHue));
    const float fraction = scaledHue - float(sector);
    const float q = 1.0f - fraction;

    switch (sector)
    {
    case 0:
        return {1.0f, fraction, 0.0f, 1.0f};
    case 1:
        return {q, 1.0f, 0.0f, 1.0f};
    case 2:
        return {0.0f, 1.0f, fraction, 1.0f};
    case 3:
        return {0.0f, q, 1.0f, 1.0f};
    case 4:
        return {fraction, 0.0f, 1.0f, 1.0f};
    default:
        return {1.0f, 0.0f, q, 1.0f};
    }
}

std::array<float, 4> resolveParticleColor(const Particle &particle, size_t particleIndex,
                                         ColorMode colorMode,
                                         const ParticleSizeColorStats &particleSizeColorStats,
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
    case ColorMode::ParticleSize:
        return orderParameterGradientColor(
            normalizedParticleSizeValue(particle.sizeParams[0], particleSizeColorStats));
    case ColorMode::Count:
        break;
    }

    const size_t colorModeIndex = static_cast<size_t>(colorMode);
    const size_t firstOrderParameterMode = static_cast<size_t>(ColorMode::Count);
    if (colorModeIndex >= firstOrderParameterMode)
    {
        const size_t orderParameterIndex = colorModeIndex - firstOrderParameterMode;
        if (orderParameterIndex < particle.orderParameters.size())
        {
            return orderParameterGradientColor(particle.orderParameters[orderParameterIndex]);
        }
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
    const ParticleSizeColorStats particleSizeColorStats =
        computeParticleSizeColorStats(particles);
    const std::vector<PatchyParticleData> *patchMetadata =
        particleSystem.hasPatchyMetadata() ? &particleSystem.patchyMetadata() : nullptr;
    for (size_t index = 0; index < particles.size(); ++index)
    {
        Particle &particle = particles[index];
        particle.color = resolveParticleColor(particle, index, colorMode,
                                              particleSizeColorStats,
                                              patchMetadata,
                                              supportsOrientationMode,
                                              uniformUsesOrientation);
    }
}

constexpr uint8_t kMinBondOrientationalOrder = 2u;
constexpr uint8_t kMaxBondOrientationalOrder2D = 6u;
constexpr uint8_t kMaxBondOrientationalOrder3D = 8u;
constexpr size_t kSteinhardtOrderCount =
    size_t(kMaxBondOrientationalOrder3D - kMinBondOrientationalOrder + 1u);
constexpr size_t kStoredSteinhardtComponentCount = 42u;
constexpr float kBondOrientationalVectorEpsilon = 1.0e-6f;

using BondOrderComplex = std::complex<float>;
using BondOrderVector = std::array<BondOrderComplex, kStoredSteinhardtComponentCount>;

uint8_t clampedBondOrientationalOrder(const ViewerState &viewerState)
{
    const uint8_t maximumOrder =
        viewerState.fileDimensionality == TrajectoryReader::Dimensionality::TwoDimensional
            ? kMaxBondOrientationalOrder2D
            : kMaxBondOrientationalOrder3D;
    return std::clamp<uint8_t>(viewerState.bondOrientationalOrder,
                               kMinBondOrientationalOrder,
                               maximumOrder);
}

constexpr size_t steinhardtArrayIndex(uint8_t order)
{
    return size_t(order - kMinBondOrientationalOrder);
}

constexpr size_t steinhardtComponentOffset(uint8_t order)
{
    return size_t((order - kMinBondOrientationalOrder) * (order + 3u) / 2u);
}

double associatedLegendrePolynomial(int l, int m, double x)
{
    x = std::clamp(x, -1.0, 1.0);

    double pmm = 1.0;
    if (m > 0)
    {
        const double sinTheta = std::sqrt(std::max(0.0, 1.0 - x * x));
        double factor = 1.0;
        for (int order = 1; order <= m; ++order)
        {
            pmm *= -factor * sinTheta;
            factor += 2.0;
        }
    }

    if (l == m)
    {
        return pmm;
    }

    double pmmp1 = x * double(2 * m + 1) * pmm;
    if (l == m + 1)
    {
        return pmmp1;
    }

    for (int order = m + 2; order <= l; ++order)
    {
        const double pll = (double(2 * order - 1) * x * pmmp1
                            - double(order + m - 1) * pmm)
                           / double(order - m);
        pmm = pmmp1;
        pmmp1 = pll;
    }

    return pmmp1;
}

double sphericalHarmonicNormalization(int l, int m)
{
    double factorialRatio = 1.0;
    for (int value = l - m + 1; value <= l + m; ++value)
    {
        factorialRatio /= double(value);
    }

    return std::sqrt(((2.0 * double(l)) + 1.0) * factorialRatio
                     / (4.0 * double(bx::kPi)));
}

BondOrderComplex sphericalHarmonicComponent(int l, int m, double cosTheta, double phi)
{
    const double amplitude = sphericalHarmonicNormalization(l, m)
                             * associatedLegendrePolynomial(l, m, cosTheta);
    const double phase = double(m) * phi;
    return {
        float(amplitude * std::cos(phase)),
        float(amplitude * std::sin(phase)),
    };
}

void scaleBondOrderVector(BondOrderVector &bondOrderVector, float scale)
{
    for (BondOrderComplex &component : bondOrderVector)
    {
        component *= scale;
    }
}

float bondOrderMagnitude(const BondOrderVector &bondOrderVector, uint8_t order)
{
    const size_t offset = steinhardtComponentOffset(order);

    double sum = double(std::norm(bondOrderVector[offset]));
    for (uint8_t m = 1u; m <= order; ++m)
    {
        sum += 2.0 * double(std::norm(bondOrderVector[offset + m]));
    }

    const float magnitude = static_cast<float>(
        std::sqrt((4.0 * double(bx::kPi) / double(2u * order + 1u)) * sum));
    return std::clamp(magnitude, 0.0f, 1.0f);
}

std::array<float, 4> resolveAnalysisColor(const ParticleAnalysisData &analysis,
                                          const ViewerState &viewerState)
{
    switch (viewerState.analysisColorMode)
    {
    case AnalysisColorMode::Disabled:
        return colorFromLetter('A');
    case AnalysisColorMode::NeighborCount:
        return colorFromLetter(
            static_cast<char>('A' + (analysis.neighborCount % kParticlePaletteColorCount)));
    case AnalysisColorMode::BondOrientationalOrderMagnitude:
        if (viewerState.fileDimensionality != TrajectoryReader::Dimensionality::TwoDimensional)
        {
            return colorFromLetter('A');
        }
        return analysisGradientColor(analysis.neighborCount == 0u
                                         ? 0.0f
                                         : analysis.bondOrientationalMagnitude);
    case AnalysisColorMode::BondOrientationalOrderPhase:
        if (viewerState.fileDimensionality != TrajectoryReader::Dimensionality::TwoDimensional)
        {
            return colorFromLetter('A');
        }
        return hueColor(analysis.neighborCount == 0u
                            ? 0.0f
                            : (analysis.bondOrientationalPhase + bx::kPi)
                                  / (2.0f * bx::kPi));
    case AnalysisColorMode::BondOrientationalQLMagnitude:
    case AnalysisColorMode::BondOrientationalQBarLMagnitude:
        if (viewerState.fileDimensionality != TrajectoryReader::Dimensionality::ThreeDimensional)
        {
            return colorFromLetter('A');
        }

        {
            const size_t orderIndex =
                steinhardtArrayIndex(clampedBondOrientationalOrder(viewerState));
            const float value = viewerState.analysisColorMode
                                        == AnalysisColorMode::BondOrientationalQLMagnitude
                                    ? analysis.steinhardtQValues[orderIndex]
                                    : analysis.steinhardtQBarValues[orderIndex];
            return analysisGradientColor(analysis.neighborCount == 0u ? 0.0f : value);
        }
    }

    return colorFromLetter('A');
}

void computeAnalysisResults(ViewerState &viewerState, ParticleSystem &particleSystem)
{
    if (!particleSystem.hasNeighborAnalysis())
    {
        particleSystem.clearAnalysisResults();
        markBondOrderScatterDataDirty(viewerState);
        return;
    }

    const std::vector<std::vector<NearestNeighborData>> &neighborLists =
        particleSystem.neighborAnalysis();
    const bool isTwoDimensional =
        viewerState.fileDimensionality == TrajectoryReader::Dimensionality::TwoDimensional;
    const uint8_t selectedOrder = clampedBondOrientationalOrder(viewerState);
    const float symmetryOrder = float(selectedOrder);

    particleSystem.resizeAnalysisResults(neighborLists.size(), 0u);
    std::vector<ParticleAnalysisData> &analysisResults = particleSystem.analysisResults();
    std::vector<BondOrderVector> qlmByParticle;
    if (!isTwoDimensional)
    {
        qlmByParticle.resize(neighborLists.size());
    }

    for (size_t index = 0; index < neighborLists.size(); ++index)
    {
        const std::vector<NearestNeighborData> &neighbors = neighborLists[index];
        ParticleAnalysisData &analysis = analysisResults[index];
        analysis.neighborCount = static_cast<uint32_t>(neighbors.size());
        analysis.bondOrientationalMagnitude = 0.0f;
        analysis.bondOrientationalPhase = 0.0f;
        analysis.bondOrientationalMagnitudes.fill(0.0f);
        analysis.bondOrientationalPhases.fill(0.0f);
        analysis.steinhardtQValues.fill(0.0f);
        analysis.steinhardtQBarValues.fill(0.0f);

        if (neighbors.empty())
        {
            continue;
        }

        if (isTwoDimensional)
        {
            std::array<float, 7u> cosSums{};
            std::array<float, 7u> sinSums{};
            for (const NearestNeighborData &neighbor : neighbors)
            {
                const float theta = std::atan2(neighbor.displacement.y,
                                               neighbor.displacement.x);
                for (uint8_t order = kMinBondOrientationalOrder;
                     order <= kMaxBondOrientationalOrder2D; ++order)
                {
                    const size_t orderIndex = steinhardtArrayIndex(order);
                    const float angle = -float(order) * theta;
                    cosSums[orderIndex] += std::cos(angle);
                    sinSums[orderIndex] += std::sin(angle);
                }
            }

            for (uint8_t order = kMinBondOrientationalOrder;
                 order <= kMaxBondOrientationalOrder2D; ++order)
            {
                const size_t orderIndex = steinhardtArrayIndex(order);
                analysis.bondOrientationalMagnitudes[orderIndex] =
                    std::sqrt(cosSums[orderIndex] * cosSums[orderIndex]
                              + sinSums[orderIndex] * sinSums[orderIndex])
                    / float(neighbors.size());
                analysis.bondOrientationalPhases[orderIndex] =
                    std::atan2(sinSums[orderIndex], cosSums[orderIndex]);
            }

            const size_t selectedOrderIndex = steinhardtArrayIndex(selectedOrder);
            analysis.bondOrientationalMagnitude =
                analysis.bondOrientationalMagnitudes[selectedOrderIndex];
            analysis.bondOrientationalPhase =
                analysis.bondOrientationalPhases[selectedOrderIndex];
            continue;
        }

        BondOrderVector &bondOrderVector = qlmByParticle[index];
        bondOrderVector.fill(BondOrderComplex(0.0f, 0.0f));
        for (const NearestNeighborData &neighbor : neighbors)
        {
            const float displacementLength = bx::length(neighbor.displacement);
            if (displacementLength <= kBondOrientationalVectorEpsilon)
            {
                continue;
            }

            const double inverseDisplacementLength = 1.0 / double(displacementLength);
            const double cosTheta = std::clamp(double(neighbor.displacement.z)
                                                   * inverseDisplacementLength,
                                               -1.0, 1.0);
            const double phi = std::atan2(double(neighbor.displacement.y),
                                          double(neighbor.displacement.x));

            for (uint8_t order = kMinBondOrientationalOrder;
                 order <= kMaxBondOrientationalOrder3D; ++order)
            {
                const size_t componentOffset = steinhardtComponentOffset(order);
                for (uint8_t m = 0u; m <= order; ++m)
                {
                    bondOrderVector[componentOffset + m] +=
                        sphericalHarmonicComponent(order, int(m), cosTheta, phi);
                }
            }
        }

        scaleBondOrderVector(bondOrderVector, 1.0f / float(neighbors.size()));
        for (uint8_t order = kMinBondOrientationalOrder;
             order <= kMaxBondOrientationalOrder3D; ++order)
        {
            analysis.steinhardtQValues[steinhardtArrayIndex(order)] =
                bondOrderMagnitude(bondOrderVector, order);
        }
    }

    if (isTwoDimensional)
    {
        markBondOrderScatterDataDirty(viewerState);
        return;
    }

    for (size_t index = 0; index < neighborLists.size(); ++index)
    {
        const std::vector<NearestNeighborData> &neighbors = neighborLists[index];
        ParticleAnalysisData &analysis = analysisResults[index];

        BondOrderVector averagedBondOrderVector{};
        averagedBondOrderVector.fill(BondOrderComplex(0.0f, 0.0f));

        size_t averageCount = 1u;
        for (size_t componentIndex = 0u; componentIndex < kStoredSteinhardtComponentCount;
             ++componentIndex)
        {
            averagedBondOrderVector[componentIndex] = qlmByParticle[index][componentIndex];
        }

        for (const NearestNeighborData &neighbor : neighbors)
        {
            if (neighbor.neighborIndex >= qlmByParticle.size())
            {
                continue;
            }

            const BondOrderVector &neighborBondOrderVector =
                qlmByParticle[neighbor.neighborIndex];
            for (size_t componentIndex = 0u; componentIndex < kStoredSteinhardtComponentCount;
                 ++componentIndex)
            {
                averagedBondOrderVector[componentIndex] +=
                    neighborBondOrderVector[componentIndex];
            }
            ++averageCount;
        }

        scaleBondOrderVector(averagedBondOrderVector, 1.0f / float(averageCount));
        for (uint8_t order = kMinBondOrientationalOrder;
             order <= kMaxBondOrientationalOrder3D; ++order)
        {
            analysis.steinhardtQBarValues[steinhardtArrayIndex(order)] =
                bondOrderMagnitude(averagedBondOrderVector, order);
        }
    }

    markBondOrderScatterDataDirty(viewerState);
}

void applyAnalysisColorMode(ParticleSystem &particleSystem, const ViewerState &viewerState)
{
    if (!viewerState.neighborAnalysisValid
        || viewerState.analysisColorMode == AnalysisColorMode::Disabled)
    {
        return;
    }

    std::vector<Particle> &particles = particleSystem.particles();
    if (!particleSystem.hasAnalysisResults(clampedBondOrientationalOrder(viewerState)))
    {
        return;
    }

    const std::vector<ParticleAnalysisData> &analysisResults = particleSystem.analysisResults();
    const size_t particleCount = bx::min(particles.size(), analysisResults.size());
    for (size_t index = 0; index < particleCount; ++index)
    {
        particles[index].color = resolveAnalysisColor(analysisResults[index], viewerState);
    }
}

void applyAnalysisColorMode(ParticleSystem &targetParticleSystem,
                            const ParticleSystem &analysisSourceParticleSystem,
                            const ViewerState &viewerState)
{
    if (!viewerState.neighborAnalysisValid
        || viewerState.analysisColorMode == AnalysisColorMode::Disabled
        || !analysisSourceParticleSystem.hasAnalysisResults(
            clampedBondOrientationalOrder(viewerState)))
    {
        return;
    }

    const std::vector<Particle> &sourceParticles = analysisSourceParticleSystem.particles();
    const std::vector<ParticleAnalysisData> &analysisResults =
        analysisSourceParticleSystem.analysisResults();
    const size_t sourceParticleCount = bx::min(sourceParticles.size(), analysisResults.size());

    std::unordered_map<uint32_t, size_t> sourceIndexById;
    sourceIndexById.reserve(sourceParticleCount);
    for (size_t index = 0; index < sourceParticleCount; ++index)
    {
        sourceIndexById.emplace(sourceParticles[index].id, index);
    }

    std::vector<Particle> &targetParticles = targetParticleSystem.particles();
    for (Particle &particle : targetParticles)
    {
        const auto sourceIndexIt = sourceIndexById.find(particle.id);
        if (sourceIndexIt == sourceIndexById.end())
        {
            continue;
        }

        particle.color = resolveAnalysisColor(analysisResults[sourceIndexIt->second],
                                              viewerState);
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

void printSelectedBondOrderParameters(const ViewerState &viewerState,
                                      const ParticleSystem &particleSystem)
{
    if (viewerState.selectedIds.empty())
    {
        std::cout << "No particles are currently selected for bond-order output."
                  << std::endl;
        return;
    }

    const uint8_t selectedOrder = clampedBondOrientationalOrder(viewerState);
    if (!particleSystem.hasAnalysisResults(selectedOrder))
    {
        std::cout << "Bond-order analysis is not available yet. Compute neighbors first."
                  << std::endl;
        return;
    }

    const std::vector<Particle> &particles = particleSystem.particles();
    const std::vector<ParticleAnalysisData> &analysisResults = particleSystem.analysisResults();
    const bool isTwoDimensional =
        viewerState.fileDimensionality == TrajectoryReader::Dimensionality::TwoDimensional;
    bool foundSelectedParticle = false;

    std::cout << std::fixed << std::setprecision(6)
              << "Bond-order values for selected particles";
    if (isTwoDimensional)
    {
        std::cout << " (symmetry = " << unsigned(selectedOrder) << ')';
    }
    else
    {
        std::cout << " (l = " << unsigned(selectedOrder) << ')';
    }
    std::cout << ':' << std::endl;

    for (size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
    {
        const Particle &particle = particles[particleIndex];
        if (!viewerState.selectedIds.contains(particle.id) || particleIndex >= analysisResults.size())
        {
            continue;
        }

        foundSelectedParticle = true;
        const ParticleAnalysisData &analysis = analysisResults[particleIndex];
        std::cout << "  Particle " << particle.id
                  << ": nearest neighbors=" << analysis.neighborCount;
        if (isTwoDimensional)
        {
            std::cout << ", |psi_" << unsigned(selectedOrder) << "|="
                      << analysis.bondOrientationalMagnitude
                      << ", phase=" << analysis.bondOrientationalPhase;
        }
        else
        {
            const size_t orderIndex = steinhardtArrayIndex(selectedOrder);
            std::cout << ", |q_" << unsigned(selectedOrder) << "|="
                      << analysis.steinhardtQValues[orderIndex]
                      << ", |qbar_" << unsigned(selectedOrder) << "|="
                      << analysis.steinhardtQBarValues[orderIndex];
        }
        std::cout << std::endl;
    }

    if (!foundSelectedParticle)
    {
        std::cout << "No selected particle IDs were found in the current frame."
                  << std::endl;
    }
}

void invalidateNeighborAnalysis(ViewerState &viewerState, ParticleSystem &particleSystem)
{
    viewerState.neighborAnalysisValid = false;
    particleSystem.clearNeighborAnalysis();
    markBondOrderScatterDataDirty(viewerState);
    markNearestNeighborRenderSystemsDirty(viewerState);
    markBondDiagramGeometryDirty(viewerState);
    if (viewerState.analysisColorMode != AnalysisColorMode::Disabled)
    {
        markColorDependentHelperSystemsDirty(viewerState);
    }
}

void logStructureFactorRenderUpdate(StructureFactorResources &structureFactorResources,
                                   bool usedGpu,
                                   bool lowResDuringInteraction,
                                   uint16_t renderSize,
                                   const std::string &gpuError)
{
    const bool unexpectedCpuFallback = !usedGpu
                                       && !gpuError.empty()
                                       && gpuError != "disabled by the Use GPU toggle";
    const bool shouldLog = unexpectedCpuFallback
                           && (!lowResDuringInteraction
                               || !structureFactorResources.hasLoggedRenderMode
                               || structureFactorResources.lastRenderUsedGpu != usedGpu
                               || structureFactorResources.lastRenderWasLowRes
                                      != lowResDuringInteraction
                               || structureFactorResources.lastRenderSize != renderSize);

    structureFactorResources.hasLoggedRenderMode = true;
    structureFactorResources.lastRenderUsedGpu = usedGpu;
    structureFactorResources.lastRenderWasLowRes = lowResDuringInteraction;
    structureFactorResources.lastRenderSize = renderSize;

    if (!shouldLog)
    {
        return;
    }

    std::cout << "Structure factor: CPU fallback"
              << (lowResDuringInteraction ? " lower-resolution during interaction" : " render")
              << " at " << renderSize << "x" << renderSize
              << " using " << structureFactorResources.particleCount << " particles"
              << " (GPU path unavailable: " << gpuError << ")." << std::endl;
}

int structureFactorModeLimitForKTimesSigma(float maxKTimesSigma, float boxLength)
{
    if (!(boxLength > 1.0e-6f))
    {
        return 1;
    }

    constexpr double kTwoPi = 6.28318530717958647692;
    const double maxWaveNumber = std::max(0.0, static_cast<double>(maxKTimesSigma));
    return std::max(1, static_cast<int>(std::ceil((maxWaveNumber * double(boxLength)) / kTwoPi)));
}

void updateStructureFactorPreview(ViewerState &viewerState,
                                  const SimulationBox &simulationBox,
                                  const ParticleSystem &particleSystem,
                                  StructureFactorResources &structureFactorResources)
{
    const bool lowResDuringInteraction = viewerState.structureFactorInteractionLowResActive;

    StructureFactorSettings settings;
    settings.previewSize = lowResDuringInteraction
                               ? std::min<uint16_t>(viewerState.structureFactorImageSize,
                                                    kInteractiveStructureFactorLowResSize)
                               : viewerState.structureFactorImageSize;
    if (viewerState.structureFactorSpecifyModeCount)
    {
        settings.maxModeX = viewerState.structureFactorMaxModeX;
        settings.maxModeY = viewerState.structureFactorMaxModeY;
    }
    else
    {
        const bx::Vec3 boxSize = simulationBox.size();
        settings.maxModeX = structureFactorModeLimitForKTimesSigma(
            viewerState.structureFactorMaxKTimesSigma, boxSize.x);
        settings.maxModeY = structureFactorModeLimitForKTimesSigma(
            viewerState.structureFactorMaxKTimesSigma, boxSize.y);
    }
    settings.blurRadius = viewerState.structureFactorBlurRadius;
    settings.colorRangeMin = viewerState.structureFactorColorRangeMin;
    settings.colorRangeMax = viewerState.structureFactorColorRangeMax;
    settings.logScale = viewerState.structureFactorLogScale;
    settings.suppressCentralPeak = viewerState.structureFactorSuppressCentralPeak;
    settings.useVisibleParticlesOnly = viewerState.structureFactorUseVisibleParticlesOnly;
    settings.allowOutOfPlaneModes =
        viewerState.fileDimensionality == TrajectoryReader::Dimensionality::ThreeDimensional;
    std::copy(std::begin(viewerState.sceneRotation), std::end(viewerState.sceneRotation),
              settings.sceneRotation.begin());

    const auto gpuStartTime = std::chrono::steady_clock::now();
    std::string gpuError;
    if (viewerState.structureFactorUseGpu)
    {
        if (renderStructureFactorPreviewGpu(settings, simulationBox, particleSystem,
                                            structureFactorResources,
                                            viewerState.structureFactorDataRevision,
                                            gpuError))
        {
            const auto gpuEndTime = std::chrono::steady_clock::now();
            structureFactorResources.computeMilliseconds =
                std::chrono::duration<float, std::milli>(gpuEndTime - gpuStartTime).count();
            structureFactorResources.statusText.clear();
            logStructureFactorRenderUpdate(structureFactorResources, true,
                                           lowResDuringInteraction, settings.previewSize, gpuError);
            viewerState.structureFactorDirty = false;
            return;
        }
    }
    else
    {
        gpuError = "disabled by the Use GPU toggle";
    }

    StructureFactorImage image;
    std::string error;
    if (!computeStructureFactorImage(particleSystem, simulationBox, settings, image, error))
    {
        destroyStructureFactorResources(structureFactorResources);
        structureFactorResources.statusText = error;
        viewerState.structureFactorDirty = true;
        return;
    }

    if (!updateStructureFactorTexture(structureFactorResources,
                                      image.width, image.height, image.rgba8Pixels))
    {
        structureFactorResources.statusText =
            structureFactorResources.disableReason.empty()
                ? "Failed to upload the structure factor image texture."
                : structureFactorResources.disableReason;
        viewerState.structureFactorDirty = true;
        return;
    }

    structureFactorResources.computeMilliseconds = image.computeMilliseconds;
    structureFactorResources.particleCount = image.particleCount;
    if (viewerState.structureFactorUseGpu
        && !gpuError.empty()
        && gpuError != "disabled by the Use GPU toggle")
    {
        structureFactorResources.statusText =
            "Warning: rendered the structure factor on the CPU because the GPU path was unavailable: "
            + gpuError;
    }
    else
    {
        structureFactorResources.statusText.clear();
    }
    logStructureFactorRenderUpdate(structureFactorResources, false,
                                   lowResDuringInteraction, settings.previewSize, gpuError);
    viewerState.structureFactorDirty = false;
}

bool usesPeriodicNeighborGrid(const ViewerState &viewerState,
                              const SimulationBox &simulationBox)
{
    if (simulationBox.shape() != SimulationBox::Shape::Rectangular)
    {
        return false;
    }

    if (!simulationBox.isPeriodic(0) || !simulationBox.isPeriodic(1))
    {
        return false;
    }

    if (viewerState.fileDimensionality == TrajectoryReader::Dimensionality::ThreeDimensional
        && !simulationBox.isPeriodic(2))
    {
        return false;
    }

    return true;
}

float wrapNeighborDelta(float delta, float extent)
{
    if (extent <= 0.0f)
    {
        return delta;
    }

    const float halfExtent = 0.5f * extent;
    while (delta > halfExtent)
    {
        delta -= extent;
    }
    while (delta < -halfExtent)
    {
        delta += extent;
    }
    return delta;
}

void addNeighborRelation(std::vector<NearestNeighborData> &neighbors, uint32_t neighborIndex,
                         const bx::Vec3 &displacement, float distance)
{
    if (neighbors.size() >= kMaxNeighborsPerParticle)
    {
        return;
    }

    neighbors.push_back(NearestNeighborData{
        .neighborIndex = neighborIndex,
        .distance = distance,
        .displacement = displacement,
    });
}

void findNearestNeighbors(const ViewerState &viewerState, const SimulationBox &simulationBox,
                          ParticleSystem &particleSystem)
{
    const std::vector<Particle> &particles = particleSystem.particles();
    particleSystem.resizeNeighborAnalysis(particles.size());
    if (particles.empty())
    {
        return;
    }

    const bool isThreeDimensional =
        viewerState.fileDimensionality == TrajectoryReader::Dimensionality::ThreeDimensional;
    const bool periodicGrid = usesPeriodicNeighborGrid(viewerState, simulationBox);

    float maxDiameter = 0.0f;
    bx::Vec3 minPosition = particles.front().position;
    bx::Vec3 maxPosition = particles.front().position;
    for (const Particle &particle : particles)
    {
        maxDiameter = bx::max(maxDiameter, 2.0f * particleRadius(particle));
        minPosition.x = bx::min(minPosition.x, particle.position.x);
        minPosition.y = bx::min(minPosition.y, particle.position.y);
        maxPosition.x = bx::max(maxPosition.x, particle.position.x);
        maxPosition.y = bx::max(maxPosition.y, particle.position.y);
        if (isThreeDimensional)
        {
            minPosition.z = bx::min(minPosition.z, particle.position.z);
            maxPosition.z = bx::max(maxPosition.z, particle.position.z);
        }
    }

    const float minimumCellSize = bx::max(maxDiameter * viewerState.neighborCutoffFactor,
                                          1.0e-6f);
    const bx::Vec3 periodicMinBounds = simulationBox.minBounds();
    const bx::Vec3 periodicBoxSize = simulationBox.size();

    bx::Vec3 origin = periodicGrid ? periodicMinBounds : minPosition;
    bx::Vec3 extent = periodicGrid ? periodicBoxSize
                                   : bx::Vec3{maxPosition.x - minPosition.x,
                                              maxPosition.y - minPosition.y,
                                              isThreeDimensional
                                                  ? (maxPosition.z - minPosition.z)
                                                  : 0.0f};
    if (!periodicGrid)
    {
        extent.x = bx::max(extent.x, minimumCellSize);
        extent.y = bx::max(extent.y, minimumCellSize);
        extent.z = isThreeDimensional ? bx::max(extent.z, minimumCellSize) : minimumCellSize;
    }

    auto cellCountForExtent = [&](float axisExtent) {
        if (periodicGrid)
        {
            return bx::max(1, int(std::floor(axisExtent / minimumCellSize)));
        }
        return bx::max(1, int(std::ceil(axisExtent / minimumCellSize)));
    };

    const int cellCountX = cellCountForExtent(extent.x);
    const int cellCountY = cellCountForExtent(extent.y);
    const int cellCountZ = isThreeDimensional ? cellCountForExtent(extent.z) : 1;

    const float cellSizeX = periodicGrid ? extent.x / float(cellCountX) : minimumCellSize;
    const float cellSizeY = periodicGrid ? extent.y / float(cellCountY) : minimumCellSize;
    const float cellSizeZ = isThreeDimensional
                                ? (periodicGrid ? extent.z / float(cellCountZ) : minimumCellSize)
                                : minimumCellSize;

    auto coordinateToCell = [&](float coordinate, float axisOrigin, float axisExtent,
                                float axisCellSize, int axisCellCount) {
        if (axisCellCount <= 1)
        {
            return 0;
        }

        float relative = coordinate - axisOrigin;
        if (periodicGrid)
        {
            while (relative < 0.0f)
            {
                relative += axisExtent;
            }
            while (relative >= axisExtent)
            {
                relative -= axisExtent;
            }
        }

        const int cellIndex = static_cast<int>(std::floor(relative / axisCellSize));
        return std::clamp(cellIndex, 0, axisCellCount - 1);
    };

    auto flattenCell = [&](int x, int y, int z) {
        return x + cellCountX * (y + cellCountY * z);
    };

    std::vector<NeighborCellAddress> particleCells(particles.size());
    std::vector<std::vector<uint32_t>> cells(
        static_cast<size_t>(cellCountX * cellCountY * cellCountZ));
    for (size_t particleIndex = 0; particleIndex < particles.size(); ++particleIndex)
    {
        const Particle &particle = particles[particleIndex];
        const int cellX = coordinateToCell(particle.position.x, origin.x, extent.x,
                                           cellSizeX, cellCountX);
        const int cellY = coordinateToCell(particle.position.y, origin.y, extent.y,
                                           cellSizeY, cellCountY);
        const int cellZ = isThreeDimensional
                              ? coordinateToCell(particle.position.z, origin.z, extent.z,
                                                 cellSizeZ, cellCountZ)
                              : 0;
        particleCells[particleIndex] = NeighborCellAddress{cellX, cellY, cellZ};
        cells[flattenCell(cellX, cellY, cellZ)].push_back(static_cast<uint32_t>(particleIndex));
    }

    std::vector<std::vector<NearestNeighborData>> &neighborLists = particleSystem.neighborAnalysis();
    for (size_t particleIndex = 0; particleIndex < particles.size(); ++particleIndex)
    {
        const Particle &particle = particles[particleIndex];
        const NeighborCellAddress sourceCell = particleCells[particleIndex];
        std::vector<int> uniqueNeighborCells;
        uniqueNeighborCells.reserve(isThreeDimensional ? 27u : 9u);
        for (int offsetZ = (isThreeDimensional ? -1 : 0);
             offsetZ <= (isThreeDimensional ? 1 : 0); ++offsetZ)
        {
            for (int offsetY = -1; offsetY <= 1; ++offsetY)
            {
                for (int offsetX = -1; offsetX <= 1; ++offsetX)
                {
                    int neighborCellX = sourceCell.x + offsetX;
                    int neighborCellY = sourceCell.y + offsetY;
                    int neighborCellZ = sourceCell.z + offsetZ;

                    if (periodicGrid)
                    {
                        neighborCellX = (neighborCellX % cellCountX + cellCountX) % cellCountX;
                        neighborCellY = (neighborCellY % cellCountY + cellCountY) % cellCountY;
                        if (isThreeDimensional)
                        {
                            neighborCellZ = (neighborCellZ % cellCountZ + cellCountZ) % cellCountZ;
                        }
                        else
                        {
                            neighborCellZ = 0;
                        }
                    }
                    else if (neighborCellX < 0 || neighborCellX >= cellCountX
                             || neighborCellY < 0 || neighborCellY >= cellCountY
                             || neighborCellZ < 0 || neighborCellZ >= cellCountZ)
                    {
                        continue;
                    }

                    const int flattenedNeighborCell =
                        flattenCell(neighborCellX, neighborCellY, neighborCellZ);
                    if (std::find(uniqueNeighborCells.begin(), uniqueNeighborCells.end(),
                                  flattenedNeighborCell)
                        != uniqueNeighborCells.end())
                    {
                        continue;
                    }
                    uniqueNeighborCells.push_back(flattenedNeighborCell);
                }
            }
        }

        for (int flattenedNeighborCell : uniqueNeighborCells)
        {
            const std::vector<uint32_t> &cellParticles = cells[flattenedNeighborCell];
                    for (uint32_t neighborIndex : cellParticles)
                    {
                        if (neighborIndex <= particleIndex)
                        {
                            continue;
                        }

                        const Particle &neighborParticle = particles[neighborIndex];
                        bx::Vec3 displacement = {
                            neighborParticle.position.x - particle.position.x,
                            neighborParticle.position.y - particle.position.y,
                            isThreeDimensional
                                ? (neighborParticle.position.z - particle.position.z)
                                : 0.0f,
                        };
                        if (periodicGrid)
                        {
                            displacement.x = wrapNeighborDelta(displacement.x, extent.x);
                            displacement.y = wrapNeighborDelta(displacement.y, extent.y);
                            if (isThreeDimensional)
                            {
                                displacement.z = wrapNeighborDelta(displacement.z, extent.z);
                            }
                        }

                        const float cutoffDistance = viewerState.neighborCutoffFactor
                                                     * (particleRadius(particle)
                                                        + particleRadius(neighborParticle));
                        const float distance = bx::length(displacement);
                        if (distance >= cutoffDistance)
                        {
                            continue;
                        }

                        addNeighborRelation(neighborLists[particleIndex], neighborIndex,
                                            displacement, distance);
                        addNeighborRelation(neighborLists[neighborIndex],
                                            static_cast<uint32_t>(particleIndex),
                                            bx::mul(displacement, -1.0f), distance);
                    }
        }
    }
}

bool ensureStructureFactorGpuResources(StructureFactorResources &structureFactorResources)
{
    if (structureFactorResources.gpuPathInitialized)
    {
        return structureFactorResources.gpuPathAvailable;
    }

    structureFactorResources.gpuPathInitialized = true;
    structureFactorResources.particleDataSampler =
        bgfx::createUniform("s_particleData", bgfx::UniformType::Sampler);
    structureFactorResources.intensitySampler =
        bgfx::createUniform("s_sfIntensity", bgfx::UniformType::Sampler);
    structureFactorResources.params0Uniform =
        bgfx::createUniform("u_sfParams0", bgfx::UniformType::Vec4);
    structureFactorResources.params1Uniform =
        bgfx::createUniform("u_sfParams1", bgfx::UniformType::Vec4);
    structureFactorResources.params2Uniform =
        bgfx::createUniform("u_sfParams2", bgfx::UniformType::Vec4);
    structureFactorResources.rotationUniform =
        bgfx::createUniform("u_sfRotation", bgfx::UniformType::Vec4, 3);
    structureFactorResources.colorParamsUniform =
        bgfx::createUniform("u_sfColorParams", bgfx::UniformType::Vec4);
    structureFactorResources.colorRangeUniform =
        bgfx::createUniform("u_sfColorRange", bgfx::UniformType::Vec4);

    bgfx::ShaderHandle computeVs = loadShader("shaders/vs_structure_factor.bin");
    bgfx::ShaderHandle computeFs = loadShader("shaders/fs_structure_factor.bin");
    bgfx::ShaderHandle colorVs = loadShader("shaders/vs_structure_factor.bin");
    bgfx::ShaderHandle colorFs = loadShader("shaders/fs_structure_factor_color.bin");
    if (!bgfx::isValid(computeVs) || !bgfx::isValid(computeFs)
        || !bgfx::isValid(colorVs) || !bgfx::isValid(colorFs))
    {
        structureFactorResources.disableReason =
            "GPU structure-factor shaders are unavailable; run compileshaders.sh to build them.";
        structureFactorResources.gpuPathAvailable = false;
        return false;
    }

    structureFactorResources.gpuProgram = bgfx::createProgram(computeVs, computeFs, true);
    structureFactorResources.colorizeProgram = bgfx::createProgram(colorVs, colorFs, true);
    if (!bgfx::isValid(structureFactorResources.gpuProgram)
        || !bgfx::isValid(structureFactorResources.colorizeProgram))
    {
        structureFactorResources.disableReason =
            "failed to create the GPU structure-factor shader programs";
        structureFactorResources.gpuPathAvailable = false;
        return false;
    }

    structureFactorResources.gpuPathAvailable = true;
    return true;
}

bool updateStructureFactorParticleDataTexture(StructureFactorResources &structureFactorResources,
                                             const StructureFactorGpuParticleData &gpuParticleData,
                                             std::string &error)
{
    error.clear();

    if (gpuParticleData.width == 0 || gpuParticleData.height == 0
        || gpuParticleData.particleCount == 0)
    {
        error = "No particle data is available for the GPU structure-factor render.";
        return false;
    }

    const size_t expectedFloatCount =
        size_t(gpuParticleData.width) * size_t(gpuParticleData.height) * 4u;
    if (gpuParticleData.rgba32fPixels.size() != expectedFloatCount)
    {
        error = "GPU particle data texture has an unexpected size.";
        return false;
    }

    constexpr uint64_t kTextureFlags = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
                                       | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

    const bgfx::TextureFormat::Enum format = bgfx::TextureFormat::RGBA32F;
    if (!bgfx::isTextureValid(0, false, 1, format, kTextureFlags))
    {
        error = "RGBA32F particle textures are not supported on this renderer.";
        return false;
    }

    const uint32_t byteSize = static_cast<uint32_t>(expectedFloatCount * sizeof(float));
    const bool needsNewTexture = !bgfx::isValid(structureFactorResources.particleDataTexture)
                                 || structureFactorResources.particleTextureWidth
                                        != gpuParticleData.width
                                 || structureFactorResources.particleTextureHeight
                                        != gpuParticleData.height
                                 || structureFactorResources.particleTextureFormat != format;
    if (needsNewTexture)
    {
        if (bgfx::isValid(structureFactorResources.particleDataTexture))
        {
            bgfx::destroy(structureFactorResources.particleDataTexture);
        }

        structureFactorResources.particleDataTexture = bgfx::createTexture2D(
            gpuParticleData.width, gpuParticleData.height, false, 1, format, kTextureFlags);
        if (!bgfx::isValid(structureFactorResources.particleDataTexture))
        {
            error = "failed to create the GPU particle data texture";
            return false;
        }

        structureFactorResources.particleTextureWidth = gpuParticleData.width;
        structureFactorResources.particleTextureHeight = gpuParticleData.height;
        structureFactorResources.particleTextureFormat = format;
    }

    const bgfx::Memory *memory = bgfx::copy(gpuParticleData.rgba32fPixels.data(), byteSize);
    bgfx::updateTexture2D(structureFactorResources.particleDataTexture, 0, 0,
                          0, 0, gpuParticleData.width, gpuParticleData.height, memory);
    return true;
}

void submitStructureFactorFullscreenTriangle(bgfx::ViewId viewId, bgfx::ProgramHandle program)
{
    static bgfx::VertexLayout vertexLayout;
    static bool vertexLayoutInitialized = false;
    if (!vertexLayoutInitialized)
    {
        ScreenSpaceVertex::init(vertexLayout);
        vertexLayoutInitialized = true;
    }

    if (bgfx::getAvailTransientVertexBuffer(3, vertexLayout) < 3)
    {
        return;
    }

    bgfx::TransientVertexBuffer transientVertexBuffer;
    bgfx::allocTransientVertexBuffer(&transientVertexBuffer, 3, vertexLayout);
    auto *vertices = reinterpret_cast<ScreenSpaceVertex *>(transientVertexBuffer.data);
    vertices[0] = {-1.0f, -1.0f, 0.0f};
    vertices[1] = { 3.0f, -1.0f, 0.0f};
    vertices[2] = {-1.0f,  3.0f, 0.0f};

    bgfx::setVertexBuffer(0, &transientVertexBuffer);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(viewId, program);
}

bool renderStructureFactorPreviewGpu(const StructureFactorSettings &settings,
                                     const SimulationBox &simulationBox,
                                     const ParticleSystem &particleSystem,
                                     StructureFactorResources &structureFactorResources,
                                     uint32_t particleDataRevision,
                                     std::string &error)
{
    error.clear();

    if (simulationBox.shape() != SimulationBox::Shape::Rectangular)
    {
        error = "GPU structure-factor rendering currently requires a rectangular simulation box.";
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
        error = "Simulation box extent Lz must be positive for GPU view-aligned sampling.";
        return false;
    }

    if (!ensureStructureFactorGpuResources(structureFactorResources))
    {
        error = structureFactorResources.disableReason.empty()
                    ? "GPU structure-factor program could not be initialized."
                    : structureFactorResources.disableReason;
        return false;
    }

    uint16_t particleTextureWidth = structureFactorResources.particleTextureWidth;
    uint16_t particleTextureHeight = structureFactorResources.particleTextureHeight;
    size_t particleCount = structureFactorResources.particleCount;

    const bool needsParticleDataUpload =
        !bgfx::isValid(structureFactorResources.particleDataTexture)
        || structureFactorResources.particleDataRevision != particleDataRevision;
    if (needsParticleDataUpload)
    {
        StructureFactorGpuParticleData gpuParticleData;
        if (!buildStructureFactorGpuParticleData(particleSystem, settings, gpuParticleData, error))
        {
            return false;
        }
        if (!updateStructureFactorParticleDataTexture(structureFactorResources,
                                                      gpuParticleData, error))
        {
            return false;
        }

        structureFactorResources.particleDataRevision = particleDataRevision;
        particleTextureWidth = gpuParticleData.width;
        particleTextureHeight = gpuParticleData.height;
        particleCount = gpuParticleData.particleCount;
    }
    else if (particleTextureWidth == 0u || particleTextureHeight == 0u || particleCount == 0u)
    {
        error = "No cached particle data is available for the GPU structure-factor render.";
        return false;
    }

    if ((!bgfx::isValid(structureFactorResources.frameBuffer)
         || structureFactorResources.width != settings.previewSize
         || structureFactorResources.height != settings.previewSize)
        && !createStructureFactorRenderTarget(structureFactorResources,
                                              settings.previewSize,
                                              settings.previewSize))
    {
        error = structureFactorResources.disableReason.empty()
                    ? "failed to create the GPU structure-factor render target"
                    : structureFactorResources.disableReason;
        return false;
    }

    const float params0[4] = {
        float(particleTextureWidth),
        float(particleTextureHeight),
        float(particleCount),
        particleCount > 0u ? 1.0f / float(particleCount) : 0.0f,
    };
    const float params1[4] = {
        float(settings.maxModeX),
        float(settings.maxModeY),
        float((2.0 * 3.14159265358979323846) / lx),
        float((2.0 * 3.14159265358979323846) / ly),
    };
    const float params2[4] = {
        settings.allowOutOfPlaneModes
            ? float((2.0 * 3.14159265358979323846) / lz)
            : 0.0f,
        settings.logScale ? 1.0f : 0.0f,
        settings.suppressCentralPeak ? 1.0f : 0.0f,
        settings.allowOutOfPlaneModes ? 1.0f : 0.0f,
    };
    const float rotation[12] = {
        settings.sceneRotation[0], settings.sceneRotation[1], settings.sceneRotation[2], 0.0f,
        settings.sceneRotation[4], settings.sceneRotation[5], settings.sceneRotation[6], 0.0f,
        settings.sceneRotation[8], settings.sceneRotation[9], settings.sceneRotation[10], 0.0f,
    };
    const float colorParams[4] = {
        structureFactorResources.width > 0u ? 1.0f / float(structureFactorResources.width) : 0.0f,
        structureFactorResources.height > 0u ? 1.0f / float(structureFactorResources.height) : 0.0f,
        float(settings.blurRadius),
        0.0f,
    };
    const float colorRangeMin = std::clamp(settings.colorRangeMin, 0.0f, 0.99f);
    const float colorRangeMax = std::clamp(settings.colorRangeMax,
                                           colorRangeMin + 0.01f, 1.0f);
    const float colorRange[4] = {
        colorRangeMin,
        colorRangeMax,
        1.0f / (colorRangeMax - colorRangeMin),
        0.0f,
    };

    bgfx::setViewName(kStructureFactorView, "StructureFactorCompute");
    bgfx::setViewRect(kStructureFactorView, 0, 0,
                      structureFactorResources.width, structureFactorResources.height);
    bgfx::setViewFrameBuffer(kStructureFactorView, structureFactorResources.intensityFrameBuffer);
    bgfx::setViewTransform(kStructureFactorView, nullptr, nullptr);
    bgfx::setViewClear(kStructureFactorView, BGFX_CLEAR_COLOR, 0x000000ff);

    bgfx::setUniform(structureFactorResources.params0Uniform, params0);
    bgfx::setUniform(structureFactorResources.params1Uniform, params1);
    bgfx::setUniform(structureFactorResources.params2Uniform, params2);
    bgfx::setUniform(structureFactorResources.rotationUniform, rotation, 3);
    bgfx::setTexture(0, structureFactorResources.particleDataSampler,
                     structureFactorResources.particleDataTexture);
    submitStructureFactorFullscreenTriangle(kStructureFactorView,
                                            structureFactorResources.gpuProgram);

    bgfx::setViewName(kStructureFactorColorView, "StructureFactorColor");
    bgfx::setViewRect(kStructureFactorColorView, 0, 0,
                      structureFactorResources.width, structureFactorResources.height);
    bgfx::setViewFrameBuffer(kStructureFactorColorView, structureFactorResources.frameBuffer);
    bgfx::setViewTransform(kStructureFactorColorView, nullptr, nullptr);
    bgfx::setViewClear(kStructureFactorColorView, BGFX_CLEAR_COLOR, 0x040406ff);
    bgfx::setUniform(structureFactorResources.colorParamsUniform, colorParams);
    bgfx::setUniform(structureFactorResources.colorRangeUniform, colorRange);
    bgfx::setTexture(0, structureFactorResources.intensitySampler,
                     structureFactorResources.intensityTexture);
    submitStructureFactorFullscreenTriangle(kStructureFactorColorView,
                                            structureFactorResources.colorizeProgram);

    structureFactorResources.particleCount = particleCount;
    structureFactorResources.enabled = true;
    return true;
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
            markPickBufferDirty(*state);
            break;
        case GLFW_KEY_LEFT:
            if ((mods & GLFW_MOD_CONTROL) != 0)
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
            if ((mods & GLFW_MOD_CONTROL) != 0)
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
            if ((mods & GLFW_MOD_CONTROL) != 0)
            {
                applySceneRotation(*state, 0.5f * bx::kPi, 0.0f, 0.0f);
                markPickBufferDirty(*state);
            }
            break;
        case GLFW_KEY_DOWN:
            if ((mods & GLFW_MOD_CONTROL) != 0)
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
            if (action == GLFW_PRESS || action == GLFW_REPEAT)
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

            const bool hadLowResInteractionRender = state->structureFactorInteractionLowResActive;
            state->leftMouseDown = false;
            state->leftDragActive = false;
            state->leftTranslateMode = false;
            if (hadLowResInteractionRender)
            {
                state->structureFactorInteractionLowResActive = false;
                if (state->structureFactorAutoUpdate && state->structureFactorDirty)
                {
                    state->structureFactorPendingCompute = true;
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
            state->rightMouseDown = false;
            if (hadLowResInteractionRender)
            {
                state->structureFactorInteractionLowResActive = false;
                if (state->structureFactorAutoUpdate && state->structureFactorDirty)
                {
                    state->structureFactorPendingCompute = true;
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
        std::cerr << "GPU picking disabled after resize: "
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
        std::cerr << viewerState.fileOpenStatusMessage << std::endl;
        return false;
    }

    ParticleSystem loadedParticleSystem(
        createParticleType(layout, openedTrajectory->fileType(), sphereStacks, sphereSlices));
    SimulationBox loadedSimulationBox = simulationBox;
    if (!openedTrajectory->loadFrame(0, loadedParticleSystem, loadedSimulationBox))
    {
        std::cerr << "Failed to load frame 0 from trajectory file: " << path;
        if (!openedTrajectory->error().empty())
        {
            std::cerr << "\n" << openedTrajectory->error();
        }
        std::cerr << std::endl;
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
    viewerState.selectedIds.clear();
    viewerState.hiddenIds.clear();
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
    viewerState.structureFactorPendingCompute = viewerState.structureFactorAutoUpdate;
    snapshotCurrentParticlePositions(particleSystem, viewerState, false);
    viewerState.fileOpenStatusMessage.clear();
    std::cout << "Opened trajectory file: " << loadedPath << std::endl;
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
        noteEncounteredParticleTypes(viewerState, particleSystem);
        invalidateNeighborAnalysis(viewerState, particleSystem);
        viewerState.pendingFindNeighbors = viewerState.autoFindNeighbors;
        applyParticleVisibilityFilters(particleSystem, viewerState);
        markAllHelperSystemsDirty(viewerState);
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
    if (viewerState.autoFindNeighbors && !viewerState.neighborAnalysisValid)
    {
        viewerState.pendingFindNeighbors = true;
    }

    if (viewerState.pendingOpenDroppedFile)
    {
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

    if (viewerState.pendingFindNeighbors)
    {
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

    if (viewerState.structureFactorPendingCompute)
    {
        updateStructureFactorPreview(viewerState, simulationBox,
                                     particleSystem, structureFactorResources);
        viewerState.structureFactorPendingCompute = false;
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
        particleSystem.setType(createParticleType(layout, particleFileType,
                                                  sphereStacks, sphereSlices));
        markAllHelperSystemsDirty(viewerState);
        markPickBufferDirty(viewerState);
        if (!hasValidParticleMeshes(particleSystem))
        {
            std::cerr << "Failed to rebuild " << particleTypeName(particleFileType)
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
        bgfx::dbgTextPrintf(0, 2, 0x0f, "Selected: %d  Last pick: %u  %s: %ux%u",
                            (int)viewerState.selectedIds.size(), viewerState.lastPickedId,
                            particleTypeName(particleFileType), sphereStacks, sphereSlices);
        bgfx::dbgTextPrintf(0, 3, 0x0f,
                            "Drag rotates. Shift+drag translates. A aligns to a selected pair. D/V print info.");
    }
    else
    {
        bgfx::dbgTextPrintf(0, 2, 0x0f, "Selected: %d  Last pick: %u  %s: %ux%u",
                            (int)viewerState.selectedIds.size(), viewerState.lastPickedId,
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
                        "Cut plane: %s  z=%.2f  Color: %s  Size: %.2f  Light: %.2f",
                        viewerState.cutPlaneEnabled ? "active" : "off",
                        viewerState.cutPlaneSceneZ,
                        currentColorModeLabel.c_str(),
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
            std::cout << viewerState.fileOpenStatusMessage << std::endl;
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
            std::cerr << "Dear ImGui UI unavailable; continuing without UI." << std::endl;
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
            std::cerr << "GPU picking disabled: " << pickResources.disableReason << std::endl;
        }
        if (!createBondDiagramResources(bondDiagramResources, kBondDiagramPreviewSize,
                                        kBondDiagramPreviewSize))
        {
            std::cerr << "Bond diagram preview unavailable: "
                      << bondDiagramResources.disableReason << std::endl;
        }

        if (exitCode == 0
            && (!hasValidParticleMeshes(particleSystem)
                || !bgfx::isValid(gpuResources.mainProgram)))
        {
            std::cerr << "INVALID BGFX RESOURCE: particleMeshes="
                      << hasValidParticleMeshes(particleSystem)
                      << " program=" << bgfx::isValid(gpuResources.mainProgram)
                      << std::endl;
            exitCode = 1;
        }
        if (exitCode == 0 && !bgfx::isValid(gpuResources.lineProgram))
        {
            std::cerr << "Wireframe box shaders unavailable" << std::endl;
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

        while (exitCode == 0 && !glfwWindowShouldClose(window.get()))
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
                           particleFileType == TrajectoryReader::FileType::Rod, false);
            applyAnalysisColorMode(particleSystem, viewerState);
            if (viewerState.mobilitySystemDirty)
            {
                rebuildMobilitySystem(particleSystem, mobilitySystem, viewerState,
                                      simulationBox);
                viewerState.mobilitySystemDirty = false;
            }
            applyColorMode(mobilitySystem, viewerState.colorMode, true, true);
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
                && !viewerState.leftMouseDown && !viewerState.rightMouseDown;
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