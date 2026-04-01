#include "ParticleSystem.h"

#include <bx/math.h>

#include <cstring>

namespace
{

constexpr uint32_t kTransformFloatCount = 16u;
constexpr uint32_t kColorFloatCount = 4u;
constexpr uint32_t kInstanceFloatCount = kTransformFloatCount + kColorFloatCount;
constexpr uint16_t kInstanceStrideBytes =
    static_cast<uint16_t>(kInstanceFloatCount * sizeof(float));

std::array<float, 4> encodeParticleIdColor(uint32_t particleId)
{
    return {float((particleId >> 24) & 0xffu) / 255.0f,
            float((particleId >> 16) & 0xffu) / 255.0f,
            float((particleId >> 8) & 0xffu) / 255.0f,
            float((particleId >> 0) & 0xffu) / 255.0f};
}

std::array<float, 4> highlightColor(const std::array<float, 4> &color)
{
    return {1.0f - color[0], 1.0f - color[1], 1.0f - color[2], color[3]};
}

bx::Vec3 transformPoint(const float *transform, const bx::Vec3 &point)
{
    if (transform == nullptr)
    {
        return point;
    }

    return {
        point.x * transform[0] + point.y * transform[4] + point.z * transform[8]
            + transform[12],
        point.x * transform[1] + point.y * transform[5] + point.z * transform[9]
            + transform[13],
        point.x * transform[2] + point.y * transform[6] + point.z * transform[10]
            + transform[14],
    };
}

} // namespace

ParticleSystem::ParticleSystem(std::unique_ptr<ParticleType> particleType)
    : m_particleType(std::move(particleType))
{
}

ParticleType &ParticleSystem::type()
{
    return *m_particleType;
}

const ParticleType &ParticleSystem::type() const
{
    return *m_particleType;
}

void ParticleSystem::setType(std::unique_ptr<ParticleType> particleType)
{
    m_particleType = std::move(particleType);
    m_instanceDataPerPart.clear();
}

void ParticleSystem::reserve(size_t count)
{
    m_particles.reserve(count);
}

void ParticleSystem::clear()
{
    m_particles.clear();
    m_patchyMetadata.clear();
}

void ParticleSystem::addParticle(const Particle &particle)
{
    m_particles.push_back(particle);
}

void ParticleSystem::addPatchyMetadata(const PatchyParticleData &patchData)
{
    m_patchyMetadata.push_back(patchData);
}

size_t ParticleSystem::size() const
{
    return m_particles.size();
}

std::vector<Particle> &ParticleSystem::particles()
{
    return m_particles;
}

const std::vector<Particle> &ParticleSystem::particles() const
{
    return m_particles;
}

bool ParticleSystem::hasPatchyMetadata() const
{
    return !m_patchyMetadata.empty() && m_patchyMetadata.size() == m_particles.size();
}

const std::vector<PatchyParticleData> &ParticleSystem::patchyMetadata() const
{
    return m_patchyMetadata;
}

void ParticleSystem::render(bgfx::ViewId viewId, bgfx::ProgramHandle program,
                            const float *parentTransform, uint64_t renderState,
                            const bx::Vec3 &positionOffset,
                            float particleSizeScale,
                            const SimulationBox *simulationBox, bool wrapToPeriodicBox,
                            const std::unordered_set<uint32_t> *selectedParticleIds,
                            const std::unordered_set<uint32_t> *highlightedParticleIds,
                            bool usePickColors, bool cutPlaneEnabled,
                            float cutPlaneSceneZ)
{
    const std::vector<RenderPart> &parts = m_particleType->renderParts();
    if (parts.empty() || m_particles.empty())
    {
        return;
    }

    if (m_instanceDataPerPart.size() != parts.size())
    {
        m_instanceDataPerPart.resize(parts.size());
    }

    for (size_t partIndex = 0; partIndex < parts.size(); ++partIndex)
    {
        const RenderPart &part = parts[partIndex];
        if (part.mesh == nullptr || !part.mesh->isValid())
        {
            continue;
        }

        std::vector<float> &instanceData = m_instanceDataPerPart[partIndex];
        instanceData.resize(m_particles.size() * kInstanceFloatCount);

        uint32_t visibleCount = 0;
        for (const Particle &particle : m_particles)
        {
            if (!particle.visible)
            {
                continue;
            }

            Particle renderParticle = particle;
            renderParticle.position.x += positionOffset.x;
            renderParticle.position.y += positionOffset.y;
            renderParticle.position.z += positionOffset.z;
            for (float &sizeParam : renderParticle.sizeParams)
            {
                sizeParam *= particleSizeScale;
            }
            if (wrapToPeriodicBox && simulationBox != nullptr)
            {
                simulationBox->wrapPosition(renderParticle.position);
            }

            if (cutPlaneEnabled)
            {
                const bx::Vec3 transformedPosition =
                    transformPoint(parentTransform, renderParticle.position);
                if (transformedPosition.z > cutPlaneSceneZ)
                {
                    continue;
                }
            }

            float *instanceBase = instanceData.data() + visibleCount * kInstanceFloatCount;
            float *outTransform = instanceBase;
            m_particleType->buildPartTransform(renderParticle, parentTransform, partIndex,
                                               outTransform);

            std::array<float, 4> visibleColor =
                usePickColors ? encodeParticleIdColor(renderParticle.id) : renderParticle.color;
            const bool isSelected = selectedParticleIds != nullptr
                                    && selectedParticleIds->contains(renderParticle.id);
            const bool isHighlighted = highlightedParticleIds != nullptr
                                       && highlightedParticleIds->contains(renderParticle.id);
            if (!usePickColors && (isSelected || isHighlighted))
            {
                visibleColor = highlightColor(visibleColor);
            }

            float *outColor = instanceBase + kTransformFloatCount;
            outColor[0] = visibleColor[0];
            outColor[1] = visibleColor[1];
            outColor[2] = visibleColor[2];
            outColor[3] = visibleColor[3];
            ++visibleCount;
        }

        if (visibleCount == 0)
        {
            continue;
        }

        bgfx::InstanceDataBuffer idb;
        bgfx::allocInstanceDataBuffer(&idb, visibleCount, kInstanceStrideBytes);
        std::memcpy(idb.data, instanceData.data(),
                    static_cast<size_t>(visibleCount) * kInstanceStrideBytes);

        part.mesh->bind();
        bgfx::setInstanceDataBuffer(&idb);
        bgfx::setState(renderState);
        bgfx::submit(viewId, program);
    }
}