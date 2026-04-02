#include "ViewerSupport.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace
{

constexpr float kInitialViewMargin = 1.10f;
constexpr uint32_t kPickBytesPerPixel = 4u;

}

float computeCutPlaneStep(const SimulationBox &simulationBox)
{
    const bx::Vec3 boxSize = simulationBox.size();
    return boxSize.z * 0.002f;
}

uint16_t clampPickCoordinate(double value, int extent)
{
    if (extent <= 0)
    {
        return 0;
    }

    const int coordinate = std::clamp(static_cast<int>(value), 0, extent - 1);
    return static_cast<uint16_t>(coordinate);
}

uint16_t mapWindowYToPickY(uint16_t windowY, uint16_t height)
{
    if (height == 0)
    {
        return 0;
    }

    if (bgfx::getCaps()->originBottomLeft)
    {
        return static_cast<uint16_t>(height - 1 - windowY);
    }

    return windowY;
}

uint32_t decodeParticleId(const uint8_t *pixel, bgfx::TextureFormat::Enum colorFormat)
{
    if (colorFormat == bgfx::TextureFormat::BGRA8)
    {
        return (uint32_t(pixel[2]) << 24) | (uint32_t(pixel[1]) << 16)
               | (uint32_t(pixel[0]) << 8) | uint32_t(pixel[3]);
    }

    return (uint32_t(pixel[0]) << 24) | (uint32_t(pixel[1]) << 16)
           | (uint32_t(pixel[2]) << 8) | uint32_t(pixel[3]);
}

void markPickBufferDirty(ViewerState &state)
{
    state.pickBufferValid = false;
    ++state.pickSceneRevision;
}

bool hasValidPickBuffer(const ViewerState &state)
{
    return state.pickBufferValid && state.cachedPickRevision == state.pickSceneRevision;
}

bool hideSelectedParticles(ParticleSystem &particleSystem,
                           std::unordered_set<uint32_t> &selectedIds,
                           std::unordered_set<uint32_t> &hiddenIds)
{
    if (selectedIds.empty())
    {
        return false;
    }

    bool changed = false;
    for (Particle &particle : particleSystem.particles())
    {
        if (selectedIds.contains(particle.id) && particle.visible)
        {
            particle.visible = false;
            hiddenIds.insert(particle.id);
            changed = true;
        }
        else if (selectedIds.contains(particle.id))
        {
            hiddenIds.insert(particle.id);
        }
    }

    selectedIds.clear();
    return changed;
}

void invertSelection(ParticleSystem &particleSystem,
                     std::unordered_set<uint32_t> &selectedIds)
{
    for (Particle &particle : particleSystem.particles())
    {
        if (!particle.visible)
        {
            continue;
        }

        if (selectedIds.contains(particle.id))
        {
            selectedIds.erase(particle.id);
        }
        else
        {
            selectedIds.insert(particle.id);
        }
    }
}

bool revealAllParticles(ParticleSystem &particleSystem,
                        std::unordered_set<uint32_t> &hiddenIds)
{
    bool changed = false;
    for (Particle &particle : particleSystem.particles())
    {
        if (!particle.visible)
        {
            particle.visible = true;
            changed = true;
        }
    }

    hiddenIds.clear();
    return changed;
}

bool applyHiddenParticles(ParticleSystem &particleSystem,
                          const std::unordered_set<uint32_t> &hiddenIds)
{
    bool changed = false;
    for (Particle &particle : particleSystem.particles())
    {
        const bool shouldBeVisible = !hiddenIds.contains(particle.id);
        if (particle.visible != shouldBeVisible)
        {
            particle.visible = shouldBeVisible;
            changed = true;
        }
    }

    return changed;
}

uint8_t particleTypeIndex(char label)
{
    const unsigned char unsignedLabel = static_cast<unsigned char>(label);
    if (!std::isalpha(unsignedLabel))
    {
        return 0u;
    }

    const int index = std::toupper(unsignedLabel) - 'A';
    return static_cast<uint8_t>(std::clamp(index, 0, int(kParticlePaletteColorCount) - 1));
}

bool isParticleTypeVisible(const ViewerState &state, char typeLabel)
{
    return state.particleTypeVisible[particleTypeIndex(typeLabel)];
}

void noteEncounteredParticleTypes(ViewerState &state, const ParticleSystem &particleSystem)
{
    for (const Particle &particle : particleSystem.particles())
    {
        state.maxSeenParticleTypeIndex = std::max(state.maxSeenParticleTypeIndex,
                                                  particleTypeIndex(particle.typeLabel));
    }
}

bool applyParticleVisibilityFilters(ParticleSystem &particleSystem,
                                    const ViewerState &state)
{
    bool changed = false;
    for (Particle &particle : particleSystem.particles())
    {
        const bool shouldBeVisible = !state.hiddenIds.contains(particle.id)
                                     && isParticleTypeVisible(state, particle.typeLabel);
        if (particle.visible != shouldBeVisible)
        {
            particle.visible = shouldBeVisible;
            changed = true;
        }
    }

    return changed;
}

void resolvePendingPickRequest(ViewerState &state, const PickResources &pickResources)
{
    if (!state.pendingPickRequest || !hasValidPickBuffer(state))
    {
        return;
    }

    const uint16_t pickY = mapWindowYToPickY(state.pendingPickY, pickResources.height);
    const size_t pixelOffset =
        (size_t(pickY) * pickResources.width + state.pendingPickX) * kPickBytesPerPixel;
    if (pixelOffset + (kPickBytesPerPixel - 1) < pickResources.readbackData.size())
    {
        state.lastPickedId =
            decodeParticleId(pickResources.readbackData.data() + pixelOffset,
                             pickResources.colorFormat);
        if (state.lastPickedId != 0)
        {
            if (!state.selectedIds.erase(state.lastPickedId))
            {
                state.selectedIds.insert(state.lastPickedId);
            }
        }
    }

    state.pendingPickRequest = false;
}

void destroyPickResources(PickResources &pickResources)
{
    if (bgfx::isValid(pickResources.frameBuffer))
    {
        bgfx::destroy(pickResources.frameBuffer);
    }

    if (bgfx::isValid(pickResources.readbackTexture))
    {
        bgfx::destroy(pickResources.readbackTexture);
    }

    if (!bgfx::isValid(pickResources.frameBuffer))
    {
        if (bgfx::isValid(pickResources.renderColorTexture))
        {
            bgfx::destroy(pickResources.renderColorTexture);
        }
        if (bgfx::isValid(pickResources.depthTexture))
        {
            bgfx::destroy(pickResources.depthTexture);
        }
    }

    pickResources.renderColorTexture = BGFX_INVALID_HANDLE;
    pickResources.readbackTexture = BGFX_INVALID_HANDLE;
    pickResources.depthTexture = BGFX_INVALID_HANDLE;
    pickResources.frameBuffer = BGFX_INVALID_HANDLE;
    pickResources.colorFormat = bgfx::TextureFormat::Count;
    pickResources.width = 0;
    pickResources.height = 0;
    pickResources.readbackData.clear();
}

bool createPickResources(PickResources &pickResources, uint16_t width, uint16_t height)
{
    destroyPickResources(pickResources);
    pickResources.enabled = false;
    pickResources.disableReason.clear();

    if ((bgfx::getCaps()->supported & BGFX_CAPS_TEXTURE_READ_BACK) == 0)
    {
        pickResources.disableReason = "texture readback unsupported";
        return false;
    }

    if ((bgfx::getCaps()->supported & BGFX_CAPS_TEXTURE_BLIT) == 0)
    {
        pickResources.disableReason = "texture blit unsupported";
        return false;
    }

    constexpr uint64_t kPickRenderColorFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_MIN_POINT
                                               | BGFX_SAMPLER_MAG_POINT
                                               | BGFX_SAMPLER_U_CLAMP
                                               | BGFX_SAMPLER_V_CLAMP;
    constexpr uint64_t kPickReadbackFlags = BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK
                                            | BGFX_SAMPLER_MIN_POINT
                                            | BGFX_SAMPLER_MAG_POINT
                                            | BGFX_SAMPLER_U_CLAMP
                                            | BGFX_SAMPLER_V_CLAMP;
    constexpr uint64_t kPickDepthFlags = BGFX_TEXTURE_RT_WRITE_ONLY | BGFX_SAMPLER_U_CLAMP
                                         | BGFX_SAMPLER_V_CLAMP;

    bgfx::TextureFormat::Enum colorFormat = bgfx::TextureFormat::RGBA8;
    if (!bgfx::isTextureValid(0, false, 1, colorFormat, kPickRenderColorFlags)
        || !bgfx::isTextureValid(0, false, 1, colorFormat, kPickReadbackFlags))
    {
        colorFormat = bgfx::TextureFormat::BGRA8;
        if (!bgfx::isTextureValid(0, false, 1, colorFormat, kPickRenderColorFlags)
            || !bgfx::isTextureValid(0, false, 1, colorFormat, kPickReadbackFlags))
        {
            pickResources.disableReason = "RGBA8/BGRA8 pick pipeline unsupported";
            return false;
        }
    }

    bgfx::TextureFormat::Enum depthFormat = bgfx::TextureFormat::D24S8;
    if (!bgfx::isTextureValid(0, false, 1, depthFormat, kPickDepthFlags))
    {
        depthFormat = bgfx::TextureFormat::D32F;
        if (!bgfx::isTextureValid(0, false, 1, depthFormat, kPickDepthFlags))
        {
            pickResources.disableReason = "depth render target unsupported";
            return false;
        }
    }

    pickResources.renderColorTexture = bgfx::createTexture2D(width, height, false, 1,
                                                             colorFormat,
                                                             kPickRenderColorFlags);
    pickResources.readbackTexture = bgfx::createTexture2D(width, height, false, 1,
                                                          colorFormat,
                                                          kPickReadbackFlags);
    pickResources.depthTexture =
        bgfx::createTexture2D(width, height, false, 1, depthFormat, kPickDepthFlags);

    if (!bgfx::isValid(pickResources.renderColorTexture)
        || !bgfx::isValid(pickResources.readbackTexture)
        || !bgfx::isValid(pickResources.depthTexture))
    {
        destroyPickResources(pickResources);
        pickResources.disableReason = "failed to create pick textures";
        return false;
    }

    bgfx::Attachment attachments[2];
    attachments[0].init(pickResources.renderColorTexture);
    attachments[1].init(pickResources.depthTexture);
    if (!bgfx::isFrameBufferValid(2, attachments))
    {
        destroyPickResources(pickResources);
        pickResources.disableReason = "pick framebuffer invalid";
        return false;
    }

    pickResources.frameBuffer = bgfx::createFrameBuffer(2, attachments, true);
    if (!bgfx::isValid(pickResources.frameBuffer))
    {
        destroyPickResources(pickResources);
        pickResources.disableReason = "failed to create pick framebuffer";
        return false;
    }

    pickResources.colorFormat = colorFormat;
    pickResources.width = width;
    pickResources.height = height;
    pickResources.readbackData.resize(size_t(width) * size_t(height) * kPickBytesPerPixel);
    pickResources.enabled = true;
    return true;
}

float computeInitialCameraDistance(const SimulationBox &simulationBox)
{
    const bx::Vec3 boxSize = simulationBox.size();
    const float halfDiagonal = 0.5f * std::sqrt(boxSize.x * boxSize.x + boxSize.y * boxSize.y
                                                + boxSize.z * boxSize.z);
    return bx::max(1.0f, 2.0f * kInitialViewMargin * halfDiagonal);
}

float computeInitialFarPlane(float cameraDistance, const SimulationBox &simulationBox)
{
    const bx::Vec3 boxSize = simulationBox.size();
    const float halfDiagonal = 0.5f * std::sqrt(boxSize.x * boxSize.x + boxSize.y * boxSize.y
                                                + boxSize.z * boxSize.z);
    return bx::max(100.0f, cameraDistance + 2.0f * halfDiagonal);
}

float computeInitialOrthoHalfHeight(const SimulationBox &simulationBox, float aspectRatio)
{
    const bx::Vec3 boxSize = simulationBox.size();
    const float safeAspectRatio = bx::max(aspectRatio, 0.001f);
    const float boxHalfWidth = 0.5f * boxSize.x;
    const float boxHalfHeight = 0.5f * boxSize.y;

    return kInitialViewMargin * bx::max(boxHalfHeight, boxHalfWidth / safeAspectRatio);
}

void applySceneRotation(ViewerState &state, float angleX, float angleY, float angleZ)
{
    float deltaRotation[16];
    bx::mtxRotateXYZ(deltaRotation, angleX, angleY, angleZ);

    float updatedRotation[16];
    bx::mtxMul(updatedRotation, state.sceneRotation, deltaRotation);
    bx::memCopy(state.sceneRotation, updatedRotation, sizeof(updatedRotation));
}