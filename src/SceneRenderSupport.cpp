#include "SceneRenderSupport.h"

#include "BxVec3Operators.h"

#include "ArrowType.h"
#include "ColorPalette.h"
#include "CubeType.h"
#include "CylinderType.h"
#include "OctahedronType.h"
#include "PatchCapType.h"
#include "PatchConeType.h"
#include "PatchPlacement.h"
#include "PolygonType.h"
#include "RodType.h"
#include "SphereType.h"
#include "VoronoiCellBuilder.h"
#include "VoronoiType.h"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace
{

constexpr bx::Vec3 kCameraTarget = {0.0f, 0.0f, 0.0f};
constexpr float kSphereRadius = 1.0f;
constexpr float kBondDiagramCoreRadius = 0.98f;
constexpr float kBondDiagramMarkerRadius = 1.05f;
constexpr float kBondDiagramCameraDistance = 4.0f;
constexpr float kBondDiagramOrthoHalfExtent = 1.35f;
constexpr std::array<float, 4> kBondDiagramCoreColor = {0.82f, 0.84f, 0.88f, 1.0f};
constexpr std::array<float, 4> kBondDiagramMixedBondGray = {0.65f, 0.65f, 0.65f, 1.0f};
constexpr float kBondDiagramMixedBondGrayWeight = 0.30f;
constexpr std::array<float, 4> kPatchColor = {0.65f, 0.65f, 0.65f, 1.0f};
constexpr bgfx::ViewId kBondDiagramView = 3;
constexpr uint64_t kParticleRenderState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                                          | BGFX_STATE_WRITE_Z
                                          | BGFX_STATE_DEPTH_TEST_LESS
                                          | BGFX_STATE_CULL_CCW
                                          | BGFX_STATE_FRONT_CCW
                                          | BGFX_STATE_MSAA;

float particleRadius(const Particle &particle)
{
    return particle.sizeParams[0];
}

std::array<float, 4> mixColor(const std::array<float, 4> &first,
                              const std::array<float, 4> &second,
                              float secondWeight)
{
    const float clampedSecondWeight = std::clamp(secondWeight, 0.0f, 1.0f);
    const float firstWeight = 1.0f - clampedSecondWeight;
    return {
        first[0] * firstWeight + second[0] * clampedSecondWeight,
        first[1] * firstWeight + second[1] * clampedSecondWeight,
        first[2] * firstWeight + second[2] * clampedSecondWeight,
        first[3] * firstWeight + second[3] * clampedSecondWeight,
    };
}

std::array<float, 4> bondDiagramColorForParticles(const Particle &firstParticle,
                                                  const Particle &secondParticle)
{
    const std::array<float, 4> firstTypeColor = colorFromLetter(firstParticle.typeLabel);
    const std::array<float, 4> secondTypeColor = colorFromLetter(secondParticle.typeLabel);
    if (std::toupper(static_cast<unsigned char>(firstParticle.typeLabel))
        == std::toupper(static_cast<unsigned char>(secondParticle.typeLabel)))
    {
        return firstTypeColor;
    }

    const std::array<float, 4> mixedTypeColor = mixColor(firstTypeColor, secondTypeColor, 0.5f);
    return mixColor(mixedTypeColor, kBondDiagramMixedBondGray,
                    kBondDiagramMixedBondGrayWeight);
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
                                                            float cutPlaneSceneZ,
                                                            bool sphericalCutEnabled,
                                                            float sphericalCutRadius)
{
    if (patchRenderSystems.coneSystem)
    {
        patchRenderSystems.coneSystem->render(viewId, program, parentTransform, renderState,
                                              positionOffset, particleSizeScale,
                                              simulationBox, wrapToPeriodicBox,
                                              selectedParticleIds, nullptr, usePickColors,
                                                                                            cutPlaneEnabled, cutPlaneSceneZ,
                                                                                            sphericalCutEnabled, sphericalCutRadius);
    }

    for (PatchCapSystem &capSystem : patchRenderSystems.capSystems)
    {
        capSystem.system->render(viewId, program, parentTransform, renderState,
                                 positionOffset, particleSizeScale,
                                 simulationBox, wrapToPeriodicBox,
                                 selectedParticleIds, nullptr, usePickColors,
                                                                cutPlaneEnabled, cutPlaneSceneZ,
                                                                sphericalCutEnabled, sphericalCutRadius);
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

void ensureBondRenderSystems(const bgfx::VertexLayout &layout, uint16_t sphereStacks,
                             uint16_t sphereSlices, BondRenderSystems &bondRenderSystems)
{
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
        bx::Vec3 displayedPosition = rawPosition + positionOffset;
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

            bx::Vec3 displacement = bondTarget->position - particle.position;
            displacement = simulationBox.nearestImage(displacement);

            const bx::Vec3 halfDisplacement = displacement * 0.5f;
            const float cylinderLength = bx::length(halfDisplacement);
            if (cylinderLength <= 1.0e-6f)
            {
                continue;
            }

            Particle cylinderParticle;
            cylinderParticle.id = particle.id;
            cylinderParticle.position = displayedSourcePosition + (0.25f * displacement);
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
                             float cutPlaneSceneZ,
                             bool sphericalCutEnabled,
                             float sphericalCutRadius)
{
    const bx::Vec3 zeroOffset{0.0f, 0.0f, 0.0f};

    if (bondRenderSystems.cylinderSystem)
    {
        bondRenderSystems.cylinderSystem->render(viewId, program, parentTransform, renderState,
                                                 zeroOffset, 1.0f,
                                                 nullptr, false,
                                                 selectedParticleIds, nullptr, usePickColors,
                                                cutPlaneEnabled, cutPlaneSceneZ,
                                                sphericalCutEnabled, sphericalCutRadius);
    }
    if (bondRenderSystems.nodeSystem)
    {
        bondRenderSystems.nodeSystem->render(viewId, program, parentTransform, renderState,
                                             zeroOffset, particleSizeScale,
                                             nullptr, false,
                                             selectedParticleIds, nullptr, usePickColors,
                                            cutPlaneEnabled, cutPlaneSceneZ,
                                            sphericalCutEnabled, sphericalCutRadius);
    }
}

void rebuildNearestNeighborRenderSystems(const ParticleSystem &particleSystem,
                                         const SimulationBox &simulationBox,
                                         const bx::Vec3 &positionOffset,
                                         bool wrapToPeriodicBox,
                                         BondRenderSystems &neighborRenderSystems)
{
    if (!particleSystem.hasNeighborAnalysis() || !neighborRenderSystems.cylinderSystem
        || !neighborRenderSystems.nodeSystem)
    {
        return;
    }

    neighborRenderSystems.cylinderSystem->clear();
    neighborRenderSystems.nodeSystem->clear();

    const auto displayedPositionFor = [&](const bx::Vec3 &rawPosition) {
        bx::Vec3 displayedPosition = rawPosition + positionOffset;
        if (wrapToPeriodicBox)
        {
            simulationBox.wrapPosition(displayedPosition);
        }
        return displayedPosition;
    };

    const std::vector<Particle> &particles = particleSystem.particles();
    const std::vector<std::vector<NearestNeighborData>> &neighborLists =
        particleSystem.neighborAnalysis();
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
        neighborRenderSystems.nodeSystem->addParticle(nodeParticle);

        for (const NearestNeighborData &neighbor : neighborLists[particleIndex])
        {
            if (neighbor.neighborIndex >= particles.size())
            {
                continue;
            }

            const Particle &neighborParticle = particles[neighbor.neighborIndex];
            if (!neighborParticle.visible)
            {
                continue;
            }

            const bx::Vec3 halfDisplacement = neighbor.displacement * 0.5f;
            const float cylinderLength = bx::length(halfDisplacement);
            if (cylinderLength <= 1.0e-6f)
            {
                continue;
            }

            Particle cylinderParticle;
            cylinderParticle.id = particle.id;
            cylinderParticle.position = displayedSourcePosition + (0.5f * halfDisplacement);
            cylinderParticle.direction = halfDisplacement;
            cylinderParticle.baseColor = particle.baseColor;
            cylinderParticle.color = particle.color;
            cylinderParticle.visible = true;
            cylinderParticle.sizeParams[0] = 0.125f * particleRadius(particle);
            cylinderParticle.sizeParams[1] = cylinderLength;
            neighborRenderSystems.cylinderSystem->addParticle(cylinderParticle);
        }
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
                                float cutPlaneSceneZ,
                                bool sphericalCutEnabled,
                                float sphericalCutRadius)
{
    for (PolygonRenderSystem &polygonRenderSystem : polygonRenderSystems.systems)
    {
        polygonRenderSystem.system->render(viewId, program, parentTransform, renderState,
                                           positionOffset, particleSizeScale,
                                           simulationBox, wrapToPeriodicBox,
                                           selectedParticleIds, nullptr, usePickColors,
                                           cutPlaneEnabled, cutPlaneSceneZ,
                                           sphericalCutEnabled, sphericalCutRadius);
    }
}

void rebuildBondDiagramSystems(const ParticleSystem &particleSystem,
                               const ViewerState &viewerState,
                               ParticleSystem &coreSystem,
                               ParticleSystem &markerSystem)
{
    coreSystem.clear();
    markerSystem.clear();

    Particle core;
    core.id = 1u;
    core.position = {0.0f, 0.0f, 0.0f};
    core.baseColor = kBondDiagramCoreColor;
    core.color = core.baseColor;
    core.visible = true;
    core.setUniformScale(kBondDiagramCoreRadius);
    coreSystem.addParticle(core);

    if (!particleSystem.hasNeighborAnalysis())
    {
        return;
    }

    const std::vector<std::vector<NearestNeighborData>> &neighborLists =
        particleSystem.neighborAnalysis();
    const std::vector<Particle> &particles = particleSystem.particles();
    uint32_t markerId = 2u;
    for (size_t particleIndex = 0; particleIndex < neighborLists.size(); ++particleIndex)
    {
        for (const NearestNeighborData &neighbor : neighborLists[particleIndex])
        {
            if (neighbor.neighborIndex >= neighborLists.size()
                || neighbor.neighborIndex >= particles.size()
                || particleIndex >= particles.size()
                || particleIndex >= neighbor.neighborIndex)
            {
                continue;
            }

            const Particle &sourceParticle = particles[particleIndex];
            const Particle &targetParticle = particles[neighbor.neighborIndex];
            const bool sourceSpeciesVisible =
                isParticleTypeVisible(viewerState, sourceParticle.typeLabel);
            const bool targetSpeciesVisible =
                isParticleTypeVisible(viewerState, targetParticle.typeLabel);
            if (!sourceSpeciesVisible && !targetSpeciesVisible)
            {
                continue;
            }

            bx::Vec3 direction = neighbor.displacement;
            const float directionLength = bx::length(direction);
            if (directionLength <= 1.0e-6f)
            {
                continue;
            }

            direction *= 1.0f / directionLength;
            const std::array<float, 4> bondColor =
                bondDiagramColorForParticles(sourceParticle, targetParticle);

            Particle forwardMarker;
            forwardMarker.id = markerId++;
            forwardMarker.position = direction * kBondDiagramMarkerRadius;
            forwardMarker.baseColor = bondColor;
            forwardMarker.color = forwardMarker.baseColor;
            forwardMarker.visible = true;
            forwardMarker.setUniformScale(viewerState.bondDiagramPointScale);
            markerSystem.addParticle(forwardMarker);

            Particle reverseMarker;
            reverseMarker.id = markerId++;
            reverseMarker.position = direction * -kBondDiagramMarkerRadius;
            reverseMarker.baseColor = bondColor;
            reverseMarker.color = reverseMarker.baseColor;
            reverseMarker.visible = true;
            reverseMarker.setUniformScale(viewerState.bondDiagramPointScale);
            markerSystem.addParticle(reverseMarker);
        }
    }
}

} // namespace

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

std::unique_ptr<ParticleType> createOctahedronParticleType(const bgfx::VertexLayout &layout)
{
    return std::make_unique<OctahedronType>(layout);
}

std::unique_ptr<ParticleType> createPolygonParticleType(const bgfx::VertexLayout &layout,
                                                        uint16_t sideCount)
{
    return std::make_unique<PolygonType>(layout, sideCount);
}

std::unique_ptr<ParticleType> createVoronoiParticleType(
    const bgfx::VertexLayout &layout,
    const std::vector<std::vector<bx::Vec3>> &pointSets,
    std::string &error)
{
    if (pointSets.empty())
    {
        error = "No Voronoi point sets were provided.";
        return nullptr;
    }

    std::vector<VoronoiMeshData> meshDataByShape;
    meshDataByShape.reserve(pointSets.size());
    for (size_t pointSetIndex = 0u; pointSetIndex < pointSets.size(); ++pointSetIndex)
    {
        VoronoiMeshData meshData;
        std::string buildError;
        if (!buildVoronoiCellMesh(pointSets[pointSetIndex], meshData, buildError))
        {
            error = "Failed to build Voronoi shape #" + std::to_string(pointSetIndex)
                    + ": " + buildError;
            return nullptr;
        }
        meshDataByShape.push_back(std::move(meshData));
    }

    error.clear();
    return std::make_unique<VoronoiType>(layout, meshDataByShape);
}

std::unique_ptr<ParticleType> createParticleType(const bgfx::VertexLayout &layout,
                                                 TrajectoryReader::FileType fileType,
                                                 const std::vector<std::vector<bx::Vec3>> &voronoiPointSets,
                                                 uint16_t stacks,
                                                 uint16_t slices)
{
    if (fileType == TrajectoryReader::FileType::Sphere
        || fileType == TrajectoryReader::FileType::BondedSphere
        || fileType == TrajectoryReader::FileType::OrderedSphere
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
    if (fileType == TrajectoryReader::FileType::Voronoi)
    {
        std::string error;
        std::unique_ptr<ParticleType> voronoiType =
            createVoronoiParticleType(layout, voronoiPointSets, error);
        if (!voronoiType)
        {
            throw std::runtime_error(error.empty() ? "Failed to create Voronoi mesh type."
                                                   : error);
        }
        return voronoiType;
    }

    return createSphereParticleType(layout, stacks, slices);
}

bool isSphereLikeFileType(TrajectoryReader::FileType fileType)
{
    return fileType == TrajectoryReader::FileType::Sphere
           || fileType == TrajectoryReader::FileType::BondedSphere
           || fileType == TrajectoryReader::FileType::OrderedSphere
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

void selectNearestNeighbors(const ParticleSystem &particleSystem,
                            std::unordered_set<uint32_t> &selectedIds)
{
    if (!particleSystem.hasNeighborAnalysis() || selectedIds.empty())
    {
        return;
    }

    const std::unordered_set<uint32_t> originalSelection = selectedIds;
    const std::vector<Particle> &particles = particleSystem.particles();
    const std::vector<std::vector<NearestNeighborData>> &neighborLists =
        particleSystem.neighborAnalysis();
    const size_t particleCount = particles.size();
    for (size_t particleIndex = 0u; particleIndex < particleCount; ++particleIndex)
    {
        if (!originalSelection.contains(particles[particleIndex].id)
            || particleIndex >= neighborLists.size())
        {
            continue;
        }

        for (const NearestNeighborData &neighbor : neighborLists[particleIndex])
        {
            const size_t neighborIndex = static_cast<size_t>(neighbor.neighborIndex);
            if (neighborIndex < particleCount)
            {
                selectedIds.insert(particles[neighborIndex].id);
            }
        }
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

void updateAuxiliaryRenderSystemsIfNeeded(ViewerState &viewerState,
                                          const bgfx::VertexLayout &layout,
                                          uint16_t sphereStacks,
                                          uint16_t sphereSlices,
                                          TrajectoryReader::FileType particleFileType,
                                          ParticleSystem &particleSystem,
                                          ParticleSystem &bondDiagramCoreSystem,
                                          ParticleSystem &bondDiagramMarkerSystem,
                                          PatchRenderSystems &patchRenderSystems,
                                          BondRenderSystems &bondRenderSystems,
                                          BondRenderSystems &nearestNeighborRenderSystems,
                                          PolygonRenderSystems &polygonRenderSystems,
                                          const SimulationBox &simulationBox)
{
    if (viewerState.bondDiagramGeometryDirty)
    {
        rebuildBondDiagramSystems(particleSystem, viewerState,
                                  bondDiagramCoreSystem,
                                  bondDiagramMarkerSystem);
        viewerState.bondDiagramGeometryDirty = false;
    }
    if (isPolygonFileType(particleFileType) && viewerState.polygonRenderSystemsDirty)
    {
        ensurePolygonRenderSystems(layout, particleSystem, polygonRenderSystems);
        rebuildPolygonRenderSystems(particleSystem, polygonRenderSystems);
        viewerState.polygonRenderSystemsDirty = false;
    }
    if (isPatchyFileType(particleFileType) && viewerState.patchRenderSystemsDirty)
    {
        ensurePatchRenderSystems(layout, sphereStacks, sphereSlices, particleSystem,
                                 patchRenderSystems);
        rebuildPatchRenderSystems(particleSystem, patchRenderSystems);
        viewerState.patchRenderSystemsDirty = false;
    }
    if ((isPatchyFileType(particleFileType) || particleSystem.hasPatchyMetadata()) 
        && viewerState.bondRenderSystemsDirty)
    {
        ensureBondRenderSystems(layout, sphereStacks, sphereSlices, bondRenderSystems);
        rebuildBondRenderSystems(particleSystem, simulationBox,
                                 viewerState.particleTranslation,
                                 viewerState.wrapParticlesToBox,
                                 bondRenderSystems);
        viewerState.bondRenderSystemsDirty = false;
    }
    if (viewerState.nearestNeighborRenderSystemsDirty)
    {
        ensureBondRenderSystems(layout, sphereStacks, sphereSlices, nearestNeighborRenderSystems);
        if (viewerState.neighborAnalysisValid)
        {
            rebuildNearestNeighborRenderSystems(particleSystem, simulationBox,
                                                viewerState.particleTranslation,
                                                viewerState.wrapParticlesToBox,
                                                nearestNeighborRenderSystems);
        }
        viewerState.nearestNeighborRenderSystemsDirty = false;
    }
}

void renderActiveScene(bgfx::ViewId viewId, bgfx::ProgramHandle program,
                       const ViewerState &viewerState,
                       TrajectoryReader::FileType particleFileType,
                       const SimulationBox &simulationBox,
                       const float *sceneTransform,
                       ParticleSystem &particleSystem,
                       ParticleSystem &mobilitySystem,
                       PatchRenderSystems &patchRenderSystems,
                       BondRenderSystems &bondRenderSystems,
                       BondRenderSystems &nearestNeighborRenderSystems,
                       PolygonRenderSystems &polygonRenderSystems,
                       bool usePickColors)
{
    const auto *selectedParticleIds =
        usePickColors ? nullptr : &viewerState.selectedIds;

    if (viewerState.bondModeEnabled)
    {
        renderBondRenderSystems(bondRenderSystems, viewId, program,
                                sceneTransform, kParticleRenderState,
                                viewerState.particleSizeScale,
                                selectedParticleIds, usePickColors,
                                viewerState.cutPlaneEnabled,
                                viewerState.cutPlaneSceneZ,
                                viewerState.sphericalCutEnabled,
                                viewerState.sphericalCutRadius);
        return;
    }

    if (viewerState.nearestNeighborModeEnabled && viewerState.neighborAnalysisValid)
    {
        renderBondRenderSystems(nearestNeighborRenderSystems, viewId, program,
                                sceneTransform, kParticleRenderState,
                                viewerState.particleSizeScale,
                                selectedParticleIds, usePickColors,
                                viewerState.cutPlaneEnabled,
                                viewerState.cutPlaneSceneZ,
                                viewerState.sphericalCutEnabled,
                                viewerState.sphericalCutRadius);
        return;
    }

    if (viewerState.mobilityModeEnabled)
    {
        mobilitySystem.render(viewId, program, sceneTransform,
                              kParticleRenderState,
                              viewerState.particleTranslation,
                              1.0f,
                              &simulationBox, viewerState.wrapParticlesToBox,
                              selectedParticleIds, nullptr,
                              usePickColors, viewerState.cutPlaneEnabled,
                              viewerState.cutPlaneSceneZ,
                              viewerState.sphericalCutEnabled,
                              viewerState.sphericalCutRadius);
        return;
    }

    if (isPolygonFileType(particleFileType))
    {
        renderPolygonRenderSystems(polygonRenderSystems, viewId, program,
                                   sceneTransform, kParticleRenderState,
                                   viewerState.particleTranslation,
                                   viewerState.particleSizeScale, &simulationBox,
                                   viewerState.wrapParticlesToBox,
                                   selectedParticleIds, usePickColors,
                                   viewerState.cutPlaneEnabled,
                                   viewerState.cutPlaneSceneZ,
                                   viewerState.sphericalCutEnabled,
                                   viewerState.sphericalCutRadius);
    }
    else
    {
        particleSystem.render(viewId, program, sceneTransform,
                              kParticleRenderState,
                              viewerState.particleTranslation,
                              viewerState.particleSizeScale, &simulationBox,
                              viewerState.wrapParticlesToBox,
                              selectedParticleIds, nullptr,
                              usePickColors,
                              viewerState.cutPlaneEnabled,
                              viewerState.cutPlaneSceneZ,
                              viewerState.sphericalCutEnabled,
                              viewerState.sphericalCutRadius);
    }

    if (isPatchyFileType(particleFileType))
    {
        renderPatchRenderSystems(patchRenderSystems, viewId, program,
                                 sceneTransform, kParticleRenderState,
                                 viewerState.particleTranslation,
                                 viewerState.particleSizeScale, &simulationBox,
                                 viewerState.wrapParticlesToBox,
                                 selectedParticleIds, usePickColors,
                                 viewerState.cutPlaneEnabled,
                                viewerState.cutPlaneSceneZ,
                                viewerState.sphericalCutEnabled,
                                viewerState.sphericalCutRadius);
    }
}

void renderBondDiagramPreview(const ViewerState &viewerState,
                              const BondDiagramResources &bondDiagramResources,
                              ParticleSystem &coreSystem,
                              ParticleSystem &markerSystem,
                              bgfx::ProgramHandle program)
{
    if (!bondDiagramResources.enabled || !bgfx::isValid(program))
    {
        return;
    }

    float view[16];
    float proj[16];
    bx::mtxLookAt(view, {0.0f, 0.0f, kBondDiagramCameraDistance}, kCameraTarget,
                  bx::Vec3{0.0f, 1.0f, 0.0f}, bx::Handedness::Right);
    bx::mtxOrtho(proj, -kBondDiagramOrthoHalfExtent, kBondDiagramOrthoHalfExtent,
                 -kBondDiagramOrthoHalfExtent, kBondDiagramOrthoHalfExtent,
                 0.1f, 10.0f, 0.0f, bgfx::getCaps()->homogeneousDepth,
                 bx::Handedness::Right);

    float sceneTransform[16];
    bx::memCopy(sceneTransform, viewerState.sceneRotation, sizeof(sceneTransform));

    bgfx::setViewRect(kBondDiagramView, 0, 0, bondDiagramResources.width,
                      bondDiagramResources.height);
    bgfx::setViewFrameBuffer(kBondDiagramView, bondDiagramResources.frameBuffer);
    bgfx::setViewTransform(kBondDiagramView, view, proj);

    const bx::Vec3 zeroOffset{0.0f, 0.0f, 0.0f};
    coreSystem.render(kBondDiagramView, program, sceneTransform, kParticleRenderState,
                      zeroOffset, 1.0f, nullptr, false, nullptr, nullptr, false,
                      false, 0.0f);
    markerSystem.render(kBondDiagramView, program, sceneTransform, kParticleRenderState,
                        zeroOffset, 1.0f, nullptr, false, nullptr, nullptr, false,
                        false, 0.0f);
}
