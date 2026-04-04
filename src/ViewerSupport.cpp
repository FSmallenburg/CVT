#include "ViewerSupport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{

constexpr float kInitialViewMargin = 1.10f;
constexpr float kAlignViewEpsilon = 1.0e-6f;
constexpr uint32_t kPickBytesPerPixel = 4u;
constexpr uint32_t kPreviewTextureBytesPerPixel = 4u;

const Particle *findParticleById(const ParticleSystem &particleSystem, uint32_t particleId)
{
    const std::vector<Particle> &particles = particleSystem.particles();
    const auto particleIt = std::find_if(
        particles.begin(), particles.end(),
        [particleId](const Particle &particle) { return particle.id == particleId; });
    return particleIt != particles.end() ? &(*particleIt) : nullptr;
}

bx::Vec3 rotateVector(const float *rotationTransform, const bx::Vec3 &vector)
{
    return {
        rotationTransform[0] * vector.x + rotationTransform[4] * vector.y
            + rotationTransform[8] * vector.z,
        rotationTransform[1] * vector.x + rotationTransform[5] * vector.y
            + rotationTransform[9] * vector.z,
        rotationTransform[2] * vector.x + rotationTransform[6] * vector.y
            + rotationTransform[10] * vector.z,
    };
}

bx::Vec3 chooseFallbackAxis(const bx::Vec3 &direction)
{
    bx::Vec3 axis = bx::cross(direction, bx::Vec3{0.0f, 1.0f, 0.0f});
    if (bx::length(axis) <= kAlignViewEpsilon)
    {
        axis = bx::cross(direction, bx::Vec3{1.0f, 0.0f, 0.0f});
    }
    return axis;
}

void resetStructureFactorPreviewTexture(StructureFactorResources &structureFactorResources)
{
    if (bgfx::isValid(structureFactorResources.frameBuffer))
    {
        bgfx::destroy(structureFactorResources.frameBuffer);
    }
    if (bgfx::isValid(structureFactorResources.intensityFrameBuffer))
    {
        bgfx::destroy(structureFactorResources.intensityFrameBuffer);
    }

    if (!bgfx::isValid(structureFactorResources.frameBuffer)
        && bgfx::isValid(structureFactorResources.colorTexture))
    {
        bgfx::destroy(structureFactorResources.colorTexture);
    }
    if (!bgfx::isValid(structureFactorResources.intensityFrameBuffer)
        && bgfx::isValid(structureFactorResources.intensityTexture))
    {
        bgfx::destroy(structureFactorResources.intensityTexture);
    }

    structureFactorResources.colorTexture = BGFX_INVALID_HANDLE;
    structureFactorResources.frameBuffer = BGFX_INVALID_HANDLE;
    structureFactorResources.intensityTexture = BGFX_INVALID_HANDLE;
    structureFactorResources.intensityFrameBuffer = BGFX_INVALID_HANDLE;
    structureFactorResources.width = 0;
    structureFactorResources.height = 0;
    structureFactorResources.enabled = false;
    structureFactorResources.disableReason.clear();
}

}

void markAllHelperSystemsDirty(ViewerState &state)
{
    state.patchRenderSystemsDirty = true;
    state.bondRenderSystemsDirty = true;
    state.nearestNeighborRenderSystemsDirty = true;
    state.polygonRenderSystemsDirty = true;
    state.mobilitySystemDirty = true;
    ++state.structureFactorDataRevision;
    if (state.structureFactorDataRevision == 0u)
    {
        state.structureFactorDataRevision = 1u;
    }
    state.structureFactorDirty = true;
    if (state.structureFactorAutoUpdate)
    {
        state.structureFactorPendingCompute = true;
    }
}

void markVisibilityDependentHelperSystemsDirty(ViewerState &state)
{
    markAllHelperSystemsDirty(state);
    markBondDiagramGeometryDirty(state);
}

void markColorDependentHelperSystemsDirty(ViewerState &state)
{
    state.bondRenderSystemsDirty = true;
    state.nearestNeighborRenderSystemsDirty = true;
    state.polygonRenderSystemsDirty = true;
}

void markBondLikeHelperSystemsDirty(ViewerState &state)
{
    state.bondRenderSystemsDirty = true;
    state.nearestNeighborRenderSystemsDirty = true;
}

void markMobilitySystemDirty(ViewerState &state)
{
    state.mobilitySystemDirty = true;
}

void markNearestNeighborRenderSystemsDirty(ViewerState &state)
{
    state.nearestNeighborRenderSystemsDirty = true;
}

void markBondDiagramGeometryDirty(ViewerState &state)
{
    state.bondDiagramGeometryDirty = true;
    state.bondDiagramViewDirty = true;
}

void markBondDiagramViewDirty(ViewerState &state)
{
    state.bondDiagramViewDirty = true;
}

void markStructureFactorDirty(ViewerState &state)
{
    state.structureFactorDirty = true;
    if (state.structureFactorAutoUpdate)
    {
        state.structureFactorPendingCompute = true;
    }
}

float computeCutPlaneStep(const SimulationBox &simulationBox)
{
    const bx::Vec3 boxSize = simulationBox.size();
    return boxSize.z * 0.01f;
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

bool alignViewToSelectedParticles(ViewerState &state,
                                  const ParticleSystem &particleSystem,
                                  const SimulationBox &simulationBox)
{
    if (state.selectedIds.size() != 2u)
    {
        std::cout << "Align view requires exactly two selected particles." << std::endl;
        return false;
    }

    std::vector<uint32_t> sortedIds(state.selectedIds.begin(), state.selectedIds.end());
    std::sort(sortedIds.begin(), sortedIds.end());

    const Particle *firstParticle = findParticleById(particleSystem, sortedIds[0]);
    const Particle *secondParticle = findParticleById(particleSystem, sortedIds[1]);
    if (firstParticle == nullptr || secondParticle == nullptr)
    {
        std::cout << "Could not resolve both selected particle IDs in the current frame."
                  << std::endl;
        return false;
    }

    //Note that we are not applying periodic wrapping to the particle positions here, 
    //so if the selected particles are on opposite sides of the box the view will be 
    //aligned to the long vector connecting them.
    bx::Vec3 displacement = {
        secondParticle->position.x - firstParticle->position.x,
        secondParticle->position.y - firstParticle->position.y,
        secondParticle->position.z - firstParticle->position.z,
    };
    

    const float displacementLength = bx::length(displacement);
    if (!(displacementLength > kAlignViewEpsilon))
    {
        std::cout << "Selected particles occupy the same position; view alignment skipped."
                  << std::endl;
        return false;
    }

    const bx::Vec3 modelDirection = {
        displacement.x / displacementLength,
        displacement.y / displacementLength,
        displacement.z / displacementLength,
    };

    bx::Vec3 sceneDirection = rotateVector(state.sceneRotation, modelDirection);
    const float sceneDirectionLength = bx::length(sceneDirection);
    if (!(sceneDirectionLength > kAlignViewEpsilon))
    {
        return false;
    }

    sceneDirection = {
        sceneDirection.x / sceneDirectionLength,
        sceneDirection.y / sceneDirectionLength,
        sceneDirection.z / sceneDirectionLength,
    };

    const bx::Vec3 targetDirection = {
        0.0f,
        0.0f,
        sceneDirection.z >= 0.0f ? 1.0f : -1.0f,
    };

    const float dotProduct = std::clamp(bx::dot(sceneDirection, targetDirection), -1.0f, 1.0f);
    if (dotProduct >= 1.0f - 1.0e-5f)
    {
        return true;
    }

    // bx uses row-vector transforms (`bx::mul(vec, mat)`), so to rotate the current
    // scene-space direction onto the view axis we need a delta matrix `D` such that
    // `sceneDirection * D == targetDirection`. That means post-multiplying the current
    // scene rotation and using the axis that rotates target<-source in row-vector form.
    bx::Vec3 rotationAxis = bx::cross(targetDirection, sceneDirection);
    float rotationAxisLength = bx::length(rotationAxis);
    if (!(rotationAxisLength > kAlignViewEpsilon))
    {
        rotationAxis = chooseFallbackAxis(sceneDirection);
        rotationAxisLength = bx::length(rotationAxis);
        if (!(rotationAxisLength > kAlignViewEpsilon))
        {
            return false;
        }
    }

    rotationAxis = {
        rotationAxis.x / rotationAxisLength,
        rotationAxis.y / rotationAxisLength,
        rotationAxis.z / rotationAxisLength,
    };

    float deltaRotation[16];
    bx::mtxFromQuaternion(deltaRotation,
                          bx::fromAxisAngle(rotationAxis, std::acos(dotProduct)));

    float updatedRotation[16];
    bx::mtxMul(updatedRotation, state.sceneRotation, deltaRotation);
    bx::memCopy(state.sceneRotation, updatedRotation, sizeof(updatedRotation));

    state.structureFactorInteractionLowResActive = false;
    markBondDiagramViewDirty(state);
    markStructureFactorDirty(state);
    markPickBufferDirty(state);
    return true;
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

void destroyBondDiagramResources(BondDiagramResources &bondDiagramResources)
{
    if (bgfx::isValid(bondDiagramResources.frameBuffer))
    {
        bgfx::destroy(bondDiagramResources.frameBuffer);
    }

    if (!bgfx::isValid(bondDiagramResources.frameBuffer))
    {
        if (bgfx::isValid(bondDiagramResources.colorTexture))
        {
            bgfx::destroy(bondDiagramResources.colorTexture);
        }
        if (bgfx::isValid(bondDiagramResources.depthTexture))
        {
            bgfx::destroy(bondDiagramResources.depthTexture);
        }
    }

    bondDiagramResources.colorTexture = BGFX_INVALID_HANDLE;
    bondDiagramResources.depthTexture = BGFX_INVALID_HANDLE;
    bondDiagramResources.frameBuffer = BGFX_INVALID_HANDLE;
    bondDiagramResources.width = 0;
    bondDiagramResources.height = 0;
    bondDiagramResources.enabled = false;
    bondDiagramResources.disableReason.clear();
}

bool createBondDiagramResources(BondDiagramResources &bondDiagramResources,
                                uint16_t width, uint16_t height)
{
    destroyBondDiagramResources(bondDiagramResources);

    constexpr uint64_t kColorFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_MIN_POINT
                                     | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP
                                     | BGFX_SAMPLER_V_CLAMP;
    constexpr uint64_t kDepthFlags = BGFX_TEXTURE_RT_WRITE_ONLY | BGFX_SAMPLER_U_CLAMP
                                     | BGFX_SAMPLER_V_CLAMP;

    bgfx::TextureFormat::Enum colorFormat = bgfx::TextureFormat::RGBA8;
    if (!bgfx::isTextureValid(0, false, 1, colorFormat, kColorFlags))
    {
        colorFormat = bgfx::TextureFormat::BGRA8;
        if (!bgfx::isTextureValid(0, false, 1, colorFormat, kColorFlags))
        {
            bondDiagramResources.disableReason = "RGBA8/BGRA8 preview render target unsupported";
            return false;
        }
    }

    bgfx::TextureFormat::Enum depthFormat = bgfx::TextureFormat::D24S8;
    if (!bgfx::isTextureValid(0, false, 1, depthFormat, kDepthFlags))
    {
        depthFormat = bgfx::TextureFormat::D32F;
        if (!bgfx::isTextureValid(0, false, 1, depthFormat, kDepthFlags))
        {
            bondDiagramResources.disableReason = "preview depth render target unsupported";
            return false;
        }
    }

    bondDiagramResources.colorTexture =
        bgfx::createTexture2D(width, height, false, 1, colorFormat, kColorFlags);
    bondDiagramResources.depthTexture =
        bgfx::createTexture2D(width, height, false, 1, depthFormat, kDepthFlags);
    if (!bgfx::isValid(bondDiagramResources.colorTexture)
        || !bgfx::isValid(bondDiagramResources.depthTexture))
    {
        destroyBondDiagramResources(bondDiagramResources);
        bondDiagramResources.disableReason = "failed to create preview textures";
        return false;
    }

    bgfx::Attachment attachments[2];
    attachments[0].init(bondDiagramResources.colorTexture);
    attachments[1].init(bondDiagramResources.depthTexture);
    if (!bgfx::isFrameBufferValid(2, attachments))
    {
        destroyBondDiagramResources(bondDiagramResources);
        bondDiagramResources.disableReason = "preview framebuffer invalid";
        return false;
    }

    bondDiagramResources.frameBuffer = bgfx::createFrameBuffer(2, attachments, true);
    if (!bgfx::isValid(bondDiagramResources.frameBuffer))
    {
        destroyBondDiagramResources(bondDiagramResources);
        bondDiagramResources.disableReason = "failed to create preview framebuffer";
        return false;
    }

    bondDiagramResources.width = width;
    bondDiagramResources.height = height;
    bondDiagramResources.enabled = true;
    return true;
}

void destroyStructureFactorResources(StructureFactorResources &structureFactorResources)
{
    resetStructureFactorPreviewTexture(structureFactorResources);

    if (bgfx::isValid(structureFactorResources.particleDataTexture))
    {
        bgfx::destroy(structureFactorResources.particleDataTexture);
    }
    if (bgfx::isValid(structureFactorResources.gpuProgram))
    {
        bgfx::destroy(structureFactorResources.gpuProgram);
    }
    if (bgfx::isValid(structureFactorResources.colorizeProgram))
    {
        bgfx::destroy(structureFactorResources.colorizeProgram);
    }
    if (bgfx::isValid(structureFactorResources.particleDataSampler))
    {
        bgfx::destroy(structureFactorResources.particleDataSampler);
    }
    if (bgfx::isValid(structureFactorResources.intensitySampler))
    {
        bgfx::destroy(structureFactorResources.intensitySampler);
    }
    if (bgfx::isValid(structureFactorResources.params0Uniform))
    {
        bgfx::destroy(structureFactorResources.params0Uniform);
    }
    if (bgfx::isValid(structureFactorResources.params1Uniform))
    {
        bgfx::destroy(structureFactorResources.params1Uniform);
    }
    if (bgfx::isValid(structureFactorResources.params2Uniform))
    {
        bgfx::destroy(structureFactorResources.params2Uniform);
    }
    if (bgfx::isValid(structureFactorResources.rotationUniform))
    {
        bgfx::destroy(structureFactorResources.rotationUniform);
    }
    if (bgfx::isValid(structureFactorResources.colorParamsUniform))
    {
        bgfx::destroy(structureFactorResources.colorParamsUniform);
    }
    if (bgfx::isValid(structureFactorResources.colorRangeUniform))
    {
        bgfx::destroy(structureFactorResources.colorRangeUniform);
    }

    structureFactorResources.particleDataTexture = BGFX_INVALID_HANDLE;
    structureFactorResources.gpuProgram = BGFX_INVALID_HANDLE;
    structureFactorResources.colorizeProgram = BGFX_INVALID_HANDLE;
    structureFactorResources.particleDataSampler = BGFX_INVALID_HANDLE;
    structureFactorResources.intensitySampler = BGFX_INVALID_HANDLE;
    structureFactorResources.params0Uniform = BGFX_INVALID_HANDLE;
    structureFactorResources.params1Uniform = BGFX_INVALID_HANDLE;
    structureFactorResources.params2Uniform = BGFX_INVALID_HANDLE;
    structureFactorResources.rotationUniform = BGFX_INVALID_HANDLE;
    structureFactorResources.colorParamsUniform = BGFX_INVALID_HANDLE;
    structureFactorResources.colorRangeUniform = BGFX_INVALID_HANDLE;
    structureFactorResources.particleTextureWidth = 0;
    structureFactorResources.particleTextureHeight = 0;
    structureFactorResources.particleTextureFormat = bgfx::TextureFormat::Count;
    structureFactorResources.gpuPathInitialized = false;
    structureFactorResources.gpuPathAvailable = false;
    structureFactorResources.statusText.clear();
    structureFactorResources.computeMilliseconds = 0.0f;
    structureFactorResources.particleCount = 0u;
    structureFactorResources.particleDataRevision = 0u;
    structureFactorResources.hasLoggedRenderMode = false;
    structureFactorResources.lastRenderUsedGpu = false;
    structureFactorResources.lastRenderWasLowRes = false;
    structureFactorResources.lastRenderSize = 0u;
}

bool createStructureFactorRenderTarget(StructureFactorResources &structureFactorResources,
                                       uint16_t width, uint16_t height)
{
    resetStructureFactorPreviewTexture(structureFactorResources);

    constexpr uint64_t kColorFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_MIN_POINT
                                     | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_U_CLAMP
                                     | BGFX_SAMPLER_V_CLAMP;

    bgfx::TextureFormat::Enum colorFormat = bgfx::TextureFormat::RGBA8;
    if (!bgfx::isTextureValid(0, false, 1, colorFormat, kColorFlags))
    {
        colorFormat = bgfx::TextureFormat::BGRA8;
        if (!bgfx::isTextureValid(0, false, 1, colorFormat, kColorFlags))
        {
            structureFactorResources.disableReason =
                "RGBA8/BGRA8 structure-factor render target unsupported";
            return false;
        }
    }

    structureFactorResources.colorTexture =
        bgfx::createTexture2D(width, height, false, 1, colorFormat, kColorFlags);
    structureFactorResources.intensityTexture =
        bgfx::createTexture2D(width, height, false, 1, colorFormat, kColorFlags);
    if (!bgfx::isValid(structureFactorResources.colorTexture)
        || !bgfx::isValid(structureFactorResources.intensityTexture))
    {
        resetStructureFactorPreviewTexture(structureFactorResources);
        structureFactorResources.disableReason = "failed to create structure-factor render target";
        return false;
    }

    bgfx::Attachment colorAttachment;
    colorAttachment.init(structureFactorResources.colorTexture);
    if (!bgfx::isFrameBufferValid(1, &colorAttachment))
    {
        resetStructureFactorPreviewTexture(structureFactorResources);
        structureFactorResources.disableReason = "structure-factor framebuffer invalid";
        return false;
    }

    bgfx::Attachment intensityAttachment;
    intensityAttachment.init(structureFactorResources.intensityTexture);
    if (!bgfx::isFrameBufferValid(1, &intensityAttachment))
    {
        resetStructureFactorPreviewTexture(structureFactorResources);
        structureFactorResources.disableReason = "structure-factor intensity framebuffer invalid";
        return false;
    }

    structureFactorResources.frameBuffer = bgfx::createFrameBuffer(1, &colorAttachment, true);
    structureFactorResources.intensityFrameBuffer =
        bgfx::createFrameBuffer(1, &intensityAttachment, true);
    if (!bgfx::isValid(structureFactorResources.frameBuffer)
        || !bgfx::isValid(structureFactorResources.intensityFrameBuffer))
    {
        resetStructureFactorPreviewTexture(structureFactorResources);
        structureFactorResources.disableReason = "failed to create structure-factor framebuffer";
        return false;
    }

    structureFactorResources.width = width;
    structureFactorResources.height = height;
    structureFactorResources.enabled = true;
    return true;
}

bool updateStructureFactorTexture(StructureFactorResources &structureFactorResources,
                                  uint16_t width, uint16_t height,
                                  const std::vector<uint8_t> &rgba8Pixels)
{
    structureFactorResources.disableReason.clear();

    if (width == 0 || height == 0)
    {
        structureFactorResources.enabled = false;
        structureFactorResources.disableReason = "preview size is zero";
        return false;
    }

    const size_t expectedSize =
        size_t(width) * size_t(height) * size_t(kPreviewTextureBytesPerPixel);
    if (rgba8Pixels.size() != expectedSize)
    {
        structureFactorResources.enabled = false;
        structureFactorResources.disableReason = "invalid preview pixel data";
        return false;
    }

    constexpr uint64_t kTextureFlags = BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
                                       | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

    bgfx::TextureFormat::Enum colorFormat = bgfx::TextureFormat::RGBA8;
    if (!bgfx::isTextureValid(0, false, 1, colorFormat, kTextureFlags))
    {
        colorFormat = bgfx::TextureFormat::BGRA8;
        if (!bgfx::isTextureValid(0, false, 1, colorFormat, kTextureFlags))
        {
            structureFactorResources.enabled = false;
            structureFactorResources.disableReason = "RGBA8/BGRA8 preview texture unsupported";
            return false;
        }
    }

    std::vector<uint8_t> uploadPixels;
    const uint8_t *uploadData = rgba8Pixels.data();
    if (colorFormat == bgfx::TextureFormat::BGRA8)
    {
        uploadPixels = rgba8Pixels;
        for (size_t index = 0; index + 3u < uploadPixels.size(); index += 4u)
        {
            std::swap(uploadPixels[index + 0u], uploadPixels[index + 2u]);
        }
        uploadData = uploadPixels.data();
    }

    const bool needsNewTexture = !bgfx::isValid(structureFactorResources.colorTexture)
                                 || bgfx::isValid(structureFactorResources.frameBuffer)
                                 || structureFactorResources.width != width
                                 || structureFactorResources.height != height;
    if (needsNewTexture)
    {
        resetStructureFactorPreviewTexture(structureFactorResources);
        structureFactorResources.colorTexture =
            bgfx::createTexture2D(width, height, false, 1, colorFormat, kTextureFlags);
        if (!bgfx::isValid(structureFactorResources.colorTexture))
        {
            structureFactorResources.enabled = false;
            structureFactorResources.disableReason = "failed to create preview texture";
            return false;
        }
    }

    const bgfx::Memory *memory = bgfx::copy(uploadData, static_cast<uint32_t>(expectedSize));
    bgfx::updateTexture2D(structureFactorResources.colorTexture, 0, 0,
                          0, 0, width, height, memory);

    structureFactorResources.width = width;
    structureFactorResources.height = height;
    structureFactorResources.enabled = true;
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

    const bool interactiveRotation =
        (state.leftMouseDown && state.leftDragActive && !state.leftTranslateMode)
        || state.rightMouseDown;
    if (interactiveRotation && state.structureFactorAutoUpdate)
    {
        state.structureFactorInteractionLowResActive = true;
    }
    else if (!interactiveRotation)
    {
        state.structureFactorInteractionLowResActive = false;
    }

    markBondDiagramViewDirty(state);
    markStructureFactorDirty(state);
}