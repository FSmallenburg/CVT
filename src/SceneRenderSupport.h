#pragma once

#include "Particle.h"
#include "ParticleSystem.h"
#include "SimulationBox.h"
#include "TrajectoryReader.h"
#include "ViewerSupport.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

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

std::unique_ptr<ParticleType> createSphereParticleType(const bgfx::VertexLayout &layout,
                                                       uint16_t stacks,
                                                       uint16_t slices);
std::unique_ptr<ParticleType> createArrowParticleType(const bgfx::VertexLayout &layout,
                                                      uint16_t slices);
std::unique_ptr<ParticleType> createCylinderParticleType(const bgfx::VertexLayout &layout,
                                                         uint16_t slices);
std::unique_ptr<ParticleType> createRodParticleType(const bgfx::VertexLayout &layout,
                                                    uint16_t stacks,
                                                    uint16_t slices);
std::unique_ptr<ParticleType> createCubeParticleType(const bgfx::VertexLayout &layout);
std::unique_ptr<ParticleType> createOctahedronParticleType(const bgfx::VertexLayout &layout);
std::unique_ptr<ParticleType> createPolygonParticleType(const bgfx::VertexLayout &layout,
                                                        uint16_t sideCount);
std::unique_ptr<ParticleType> createVoronoiParticleType(
    const bgfx::VertexLayout &layout,
    const std::vector<std::vector<bx::Vec3>> &pointSets,
    std::string &error);
std::unique_ptr<ParticleType> createParticleType(const bgfx::VertexLayout &layout,
                                                 TrajectoryReader::FileType fileType,
                                                 const std::vector<std::vector<bx::Vec3>> &voronoiPointSets,
                                                 uint16_t stacks,
                                                 uint16_t slices);

bool isSphereLikeFileType(TrajectoryReader::FileType fileType);
bool isPatchyFileType(TrajectoryReader::FileType fileType);
bool isPolygonFileType(TrajectoryReader::FileType fileType);
uint16_t polygonSideCount(const Particle &particle);

void selectBondedNeighbors(const ParticleSystem &particleSystem,
                           std::unordered_set<uint32_t> &selectedIds);
void rebuildMobilitySystem(const ParticleSystem &particleSystem,
                           ParticleSystem &mobilitySystem,
                           const ViewerState &viewerState,
                           const SimulationBox &simulationBox);
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
                                          const SimulationBox &simulationBox);
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
                       bool usePickColors);
void renderBondDiagramPreview(const ViewerState &viewerState,
                              const BondDiagramResources &bondDiagramResources,
                              ParticleSystem &coreSystem,
                              ParticleSystem &markerSystem,
                              bgfx::ProgramHandle program);
