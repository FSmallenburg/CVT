#include "AnalysisSupport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace
{

constexpr float kOrientationEpsilon = 1.0e-6f;
constexpr uint32_t kMaxNeighborsPerParticle = 50u;

struct NeighborCellAddress
{
    int x = 0;
    int y = 0;
    int z = 0;
};

struct ParticleSizeColorStats computeParticleSizeColorStats(const std::vector<Particle> &particles)
{
    ParticleSizeColorStats stats{};
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

std::array<float, 4> orientationColor(const bx::Vec3 &direction)
{
    const float length = bx::length(direction);
    if (length <= kOrientationEpsilon)
    {
        return colorFromLetter('A');
    }

    const float inverseLength = 1.0f / length;
    return {std::abs(direction.x) * inverseLength,
            std::abs(direction.y) * inverseLength,
            std::abs(direction.z) * inverseLength,
            1.0f};
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

std::array<float, 4> resolveParticleColor(const Particle &particle,
                                          size_t particleIndex,
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
            return colorFromLetter(
                static_cast<char>('A' + (bondCount % kParticlePaletteColorCount)));
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

float particleRadius(const Particle &particle)
{
    return particle.sizeParams[0];
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

void addNeighborRelation(std::vector<NearestNeighborData> &neighbors,
                         uint32_t neighborIndex,
                         const bx::Vec3 &displacement,
                         float distance)
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

} // namespace

void applyColorMode(ParticleSystem &particleSystem,
                    ColorMode colorMode,
                    bool supportsOrientationMode,
                    bool uniformUsesOrientation,
                    ParticleColorStatsCache &statsCache)
{
    std::vector<Particle> &particles = particleSystem.particles();

    if (particles.size() != statsCache.particleCount
        || colorMode != statsCache.colorMode)
    {
        statsCache.stats = computeParticleSizeColorStats(particles);
        statsCache.particleCount = particles.size();
        statsCache.colorMode = colorMode;
    }

    const std::vector<PatchyParticleData> *patchMetadata =
        particleSystem.hasPatchyMetadata() ? &particleSystem.patchyMetadata() : nullptr;
    for (size_t index = 0; index < particles.size(); ++index)
    {
        Particle &particle = particles[index];
        particle.color = resolveParticleColor(particle,
                                              index,
                                              colorMode,
                                              statsCache.stats,
                                              patchMetadata,
                                              supportsOrientationMode,
                                              uniformUsesOrientation);
    }
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
                                               -1.0,
                                               1.0);
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
        if (!viewerState.selectedIds.contains(particle.id)
            || particleIndex >= analysisResults.size())
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

void findNearestNeighbors(const ViewerState &viewerState,
                          const SimulationBox &simulationBox,
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

                addNeighborRelation(neighborLists[particleIndex],
                                    neighborIndex,
                                    displacement,
                                    distance);
                addNeighborRelation(neighborLists[neighborIndex],
                                    static_cast<uint32_t>(particleIndex),
                                    bx::mul(displacement, -1.0f),
                                    distance);
            }
        }
    }
}
