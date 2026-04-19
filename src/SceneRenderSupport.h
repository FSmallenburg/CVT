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

/// Holds the two auxiliary ParticleSystems used to render patch cones at one
/// opening angle, together with the mesh resolution parameters.
struct PatchCapSystem
{
    float cosHalfAngle = 1.0f;        ///< The half-angle this cap system was built for.
    std::unique_ptr<ParticleSystem> system;
};

/// Aggregates all render systems needed to draw patchy-particle geometry:
/// one cone system for all patches, plus one cap system per distinct half-angle.
struct PatchRenderSystems
{
    std::unique_ptr<ParticleSystem> coneSystem;
    std::vector<PatchCapSystem> capSystems;
    uint16_t coneSlices = 0u;   ///< Mesh resolution currently built (0 = not yet built).
    uint16_t capStacks  = 0u;
    uint16_t capSlices  = 0u;
};

/// Aggregates the cylinder and end-sphere systems used to render bonds or
/// nearest-neighbor connection lines.
struct BondRenderSystems
{
    std::unique_ptr<ParticleSystem> cylinderSystem; ///< Cylinder segments between bonded particles.
    std::unique_ptr<ParticleSystem> nodeSystem;     ///< Spheres placed at each bond end-point.
    uint16_t cylinderSlices = 0u;
    uint16_t sphereStacks   = 0u;
    uint16_t sphereSlices   = 0u;
};

/// Pairs a polygon side-count with the ParticleSystem that renders it.
/// Multiple instances exist when a file contains polygons of different shapes.
struct PolygonRenderSystem
{
    uint16_t sideCount = 0u;
    std::unique_ptr<ParticleSystem> system;
};

/// Collection of PolygonRenderSystem entries, one per distinct polygon type.
struct PolygonRenderSystems
{
    std::vector<PolygonRenderSystem> systems;
};

// ── ParticleType factory helpers ──────────────────────────────────────────────

/// Creates a SphereType at the given mesh resolution.
std::unique_ptr<ParticleType> createSphereParticleType(const bgfx::VertexLayout &layout,
                                                       uint16_t stacks,
                                                       uint16_t slices);
/// Creates an ArrowType at the given mesh resolution.
std::unique_ptr<ParticleType> createArrowParticleType(const bgfx::VertexLayout &layout,
                                                      uint16_t slices);
/// Creates a CylinderType at the given mesh resolution.
std::unique_ptr<ParticleType> createCylinderParticleType(const bgfx::VertexLayout &layout,
                                                         uint16_t slices);
/// Creates a RodType (capped cylinder) at the given mesh resolution.
std::unique_ptr<ParticleType> createRodParticleType(const bgfx::VertexLayout &layout,
                                                    uint16_t stacks,
                                                    uint16_t slices);
/// Creates a CubeType.
std::unique_ptr<ParticleType> createCubeParticleType(const bgfx::VertexLayout &layout);
/// Creates an OctahedronType.
std::unique_ptr<ParticleType> createOctahedronParticleType(const bgfx::VertexLayout &layout);
/// Creates a PolygonType with @p sideCount sides.
std::unique_ptr<ParticleType> createPolygonParticleType(const bgfx::VertexLayout &layout,
                                                        uint16_t sideCount);
/// Creates a VoronoiType from pre-computed Voronoi cell point sets.
/// Returns nullptr and sets @p error on failure.
std::unique_ptr<ParticleType> createVoronoiParticleType(
    const bgfx::VertexLayout &layout,
    const std::vector<std::vector<bx::Vec3>> &pointSets,
    std::string &error);
/// Selects and constructs the appropriate ParticleType for @p fileType.
/// For Voronoi files, @p voronoiPointSets must be non-empty.
std::unique_ptr<ParticleType> createParticleType(const bgfx::VertexLayout &layout,
                                                 TrajectoryReader::FileType fileType,
                                                 const std::vector<std::vector<bx::Vec3>> &voronoiPointSets,
                                                 uint16_t stacks,
                                                 uint16_t slices);

// ── File-type classification helpers ─────────────────────────────────────────

/// Returns true for file types whose primary particles are spheres or sphere-like
/// (Sphere, BondedSphere, OrderedSphere).
bool isSphereLikeFileType(TrajectoryReader::FileType fileType);
/// Returns true for file types that include patchy-particle metadata.
bool isPatchyFileType(TrajectoryReader::FileType fileType);
/// Returns true for file types that use polygon geometry.
bool isPolygonFileType(TrajectoryReader::FileType fileType);
/// Returns the number of polygon sides encoded in @p particle.sizeParams.
uint16_t polygonSideCount(const Particle &particle);

// ── Render-system update and drawing ─────────────────────────────────────────

/// Adds all bonded-neighbor IDs of selected particles to @p selectedIds.
void selectBondedNeighbors(const ParticleSystem &particleSystem,
                           std::unordered_set<uint32_t> &selectedIds);

/// Rebuilds the arrow-based mobility system from per-particle displacement
/// vectors (previousRawPositions vs. current positions) stored in @p viewerState.
void rebuildMobilitySystem(const ParticleSystem &particleSystem,
                           ParticleSystem &mobilitySystem,
                           const ViewerState &viewerState,
                           const SimulationBox &simulationBox);

/// Checks all dirty flags in @p viewerState and rebuilds whichever auxiliary
/// render systems (patches, bonds, nearest-neighbor lines, polygons, bond
/// diagram) are out of date. Clears the corresponding dirty flags on return.
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

/// Submits all visible particle systems (primary, mobility, patches, bonds,
/// nearest-neighbor lines, polygons) to the given bgfx @p viewId.
/// When @p usePickColors is true, particles are rendered with unique encoded
/// colors for off-screen picking.
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

/// Renders the bond-order diagram preview into its off-screen frame buffer.
/// @p coreSystem holds the central sphere particles and @p markerSystem holds
/// the surrounding bond-order marker spheres.
void renderBondDiagramPreview(const ViewerState &viewerState,
                              const BondDiagramResources &bondDiagramResources,
                              ParticleSystem &coreSystem,
                              ParticleSystem &markerSystem,
                              bgfx::ProgramHandle program);
