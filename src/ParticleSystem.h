#pragma once

#include "Particle.h"
#include "ParticleType.h"
#include "SimulationBox.h"

#include <bgfx/bgfx.h>

#include <array>
#include <memory>
#include <unordered_set>
#include <vector>

/// Per-particle orientation and patch geometry for patchy-particle file types.
struct PatchyParticleData
{
    /// Row-major 3×3 rotation matrix describing the particle's orientation.
    std::array<float, 9> orientationMatrix{1.0f, 0.0f, 0.0f,
                                           0.0f, 1.0f, 0.0f,
                                           0.0f, 0.0f, 1.0f};
    float coreRadius = 1.0f;
    /// cos(half-opening-angle) of the patch cone.
    float cosHalfAngle = 1.0f;
    float capRadius = 1.0f;
    /// True for 2-D patchy files where patches are placed in the XY plane.
    bool planarPlacement = false;
    /// Indices into the _bonded_ particle list (negative = no bond).
    std::vector<int32_t> bondIds;
};

/// A single entry in a particle's nearest-neighbor list.
struct NearestNeighborData
{
    /// Index into ParticleSystem::particles() of the neighboring particle.
    uint32_t neighborIndex = 0;
    float distance = 0.0f;
    /// Minimum-image displacement vector from this particle to the neighbor.
    bx::Vec3 displacement{0.0f, 0.0f, 0.0f};
};

/// Bond-orientational-order analysis results for a single particle.
/// Populated by computeAnalysisResults().
struct ParticleAnalysisData
{
    uint32_t neighborCount = 0;
    /// |q_l| for the configured bond-orientational order l.
    float bondOrientationalMagnitude = 0.0f;
    /// arg(q_l) for the configured bond-orientational order l.
    float bondOrientationalPhase = 0.0f;
    /// |q_lm| for l = 0 … 6 (indices match the l value).
    std::array<float, 7u> bondOrientationalMagnitudes{};
    std::array<float, 7u> bondOrientationalPhases{};
    /// Steinhardt Q_l for l = 0 … 6 (local order parameter).
    std::array<float, 7u> steinhardtQValues{};
    /// Steinhardt Q̄_l for l = 0 … 6 (coarse-grained / averaged order parameter).
    std::array<float, 7u> steinhardtQBarValues{};
};

/// Owns a collection of particles of a single type, together with optional
/// per-particle metadata (patchy, nearest-neighbor, analysis). Rendering is
/// delegated to the ParticleType held inside.
class ParticleSystem
{
  public:
    explicit ParticleSystem(std::unique_ptr<ParticleType> particleType);

    /// Returns a mutable reference to the active ParticleType (e.g. SphereType).
    ParticleType &type();
    const ParticleType &type() const;

    /// Replaces the particle type. The render resources of the old type are
    /// destroyed; the new type's resources are created on the next render call.
    void setType(std::unique_ptr<ParticleType> particleType);

    /// Pre-allocates space for @p count particles (optional optimisation).
    void reserve(size_t count);

    /// Removes all particles and clears all metadata vectors.
    void clear();

    void addParticle(const Particle &particle);
    void addPatchyMetadata(const PatchyParticleData &patchData);

    /// Returns the number of particles currently in the system.
    size_t size() const;

    std::vector<Particle> &particles();
    const std::vector<Particle> &particles() const;

    /// Returns true when a patchy-metadata entry exists for each particle.
    bool hasPatchyMetadata() const;
    std::vector<PatchyParticleData> &patchyMetadata();
    const std::vector<PatchyParticleData> &patchyMetadata() const;
    void clearPatchyMetadata();

    /// Discards the nearest-neighbor lists computed by findNearestNeighbors().
    void clearNeighborAnalysis();
    /// Allocates empty neighbor-analysis entries for @p count particles.
    void resizeNeighborAnalysis(size_t count);
    /// Returns true when neighbor lists have been computed for all particles.
    bool hasNeighborAnalysis() const;
    std::vector<std::vector<NearestNeighborData>> &neighborAnalysis();
    const std::vector<std::vector<NearestNeighborData>> &neighborAnalysis() const;

    /// Discards bond-orientational-order results.
    void clearAnalysisResults();
    /// Allocates empty analysis entries for @p count particles at order @p bondOrientationalOrder.
    void resizeAnalysisResults(size_t count, uint8_t bondOrientationalOrder);
    /// Returns true when analysis results exist and were computed at @p bondOrientationalOrder.
    bool hasAnalysisResults(uint8_t bondOrientationalOrder) const;
    std::vector<ParticleAnalysisData> &analysisResults();
    const std::vector<ParticleAnalysisData> &analysisResults() const;

    /// Submits all visible particles to bgfx for rendering.
    ///
    /// @param viewId              bgfx view to render into.
    /// @param program             Shader program to use.
    /// @param parentTransform     Column-major 4×4 world transform applied on top of per-particle transforms.
    /// @param renderState         bgfx render-state flags (blend, depth, cull, …).
    /// @param positionOffset      Additional world-space translation added to every particle position.
    /// @param particleSizeScale   Uniform scale multiplier applied to particle radii/extents.
    /// @param simulationBox       When non-null and @p wrapToPeriodicBox is true, particles are
    ///                            wrapped into the periodic cell before rendering.
    /// @param wrapToPeriodicBox   Whether to apply minimum-image wrapping.
    /// @param selectedParticleIds When non-null, particles in this set are rendered with selection highlighting.
    /// @param highlightedParticleIds When non-null, particles in this set receive an additional highlight.
    /// @param usePickColors       When true, particles are rendered with unique ID-encoded pick colors
    ///                            instead of their normal colors.
    /// @param cutPlaneEnabled     Enables the horizontal cut-plane clipping.
    /// @param cutPlaneSceneZ      World-space Z coordinate of the cut plane.
    void render(bgfx::ViewId viewId, bgfx::ProgramHandle program, const float *parentTransform,
          uint64_t renderState, const bx::Vec3 &positionOffset = {0.0f, 0.0f, 0.0f},
          float particleSizeScale = 1.0f,
          const SimulationBox *simulationBox = nullptr,
          bool wrapToPeriodicBox = false,
          const std::unordered_set<uint32_t> *selectedParticleIds = nullptr,
          const std::unordered_set<uint32_t> *highlightedParticleIds = nullptr,
          bool usePickColors = false, bool cutPlaneEnabled = false,
          float cutPlaneSceneZ = 0.0f);

  private:
    std::unique_ptr<ParticleType> m_particleType;
    std::vector<Particle> m_particles;
    std::vector<PatchyParticleData> m_patchyMetadata;
    std::vector<std::vector<NearestNeighborData>> m_neighborAnalysis;
    std::vector<ParticleAnalysisData> m_analysisResults;
    /// The bond-orientational order l used when m_analysisResults were last filled.
    uint8_t m_cachedBondOrientationalOrder = 0u;
    /// Per-render-part instance data buffers, reused across frames.
    std::vector<std::vector<float>> m_instanceDataPerPart;
};