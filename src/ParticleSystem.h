#pragma once

#include "Particle.h"
#include "ParticleType.h"
#include "SimulationBox.h"

#include <bgfx/bgfx.h>

#include <memory>
#include <unordered_set>
#include <vector>

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
    size_t size() const;

    std::vector<Particle> &particles();
    const std::vector<Particle> &particles() const;

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
    std::vector<std::vector<float>> m_instanceDataPerPart;
};