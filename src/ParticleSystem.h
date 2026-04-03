#pragma once

#include "Particle.h"
#include "ParticleType.h"
#include "SimulationBox.h"

#include <bgfx/bgfx.h>

#include <array>
#include <memory>
#include <unordered_set>
#include <vector>

struct PatchyParticleData
{
    std::array<float, 9> orientationMatrix{1.0f, 0.0f, 0.0f,
                                           0.0f, 1.0f, 0.0f,
                                           0.0f, 0.0f, 1.0f};
    float coreRadius = 1.0f;
    float cosHalfAngle = 1.0f;
    float capRadius = 1.0f;
    bool planarPlacement = false;
    std::vector<int32_t> bondIds;
};

  struct NearestNeighborData
  {
    uint32_t neighborIndex = 0;
    float distance = 0.0f;
    bx::Vec3 displacement{0.0f, 0.0f, 0.0f};
  };

  struct ParticleAnalysisData
  {
    uint32_t neighborCount = 0;
    float bondOrientationalMagnitude = 0.0f;
    float bondOrientationalPhase = 0.0f;
  };

class ParticleSystem
{
  public:
    explicit ParticleSystem(std::unique_ptr<ParticleType> particleType);

    ParticleType &type();
    const ParticleType &type() const;
    void setType(std::unique_ptr<ParticleType> particleType);

    void reserve(size_t count);
    void clear();
    void addParticle(const Particle &particle);
    void addPatchyMetadata(const PatchyParticleData &patchData);
    size_t size() const;

    std::vector<Particle> &particles();
    const std::vector<Particle> &particles() const;
    bool hasPatchyMetadata() const;
    const std::vector<PatchyParticleData> &patchyMetadata() const;
    void clearNeighborAnalysis();
    void resizeNeighborAnalysis(size_t count);
    bool hasNeighborAnalysis() const;
    std::vector<std::vector<NearestNeighborData>> &neighborAnalysis();
    const std::vector<std::vector<NearestNeighborData>> &neighborAnalysis() const;
    void clearAnalysisResults();
    void resizeAnalysisResults(size_t count, uint8_t bondOrientationalOrder);
    bool hasAnalysisResults(uint8_t bondOrientationalOrder) const;
    std::vector<ParticleAnalysisData> &analysisResults();
    const std::vector<ParticleAnalysisData> &analysisResults() const;

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
    uint8_t m_cachedBondOrientationalOrder = 0u;
    std::vector<std::vector<float>> m_instanceDataPerPart;
};