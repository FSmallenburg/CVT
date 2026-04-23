#include "AnalysisSupport.h"
#include "BxVec3Operators.h"
#include "Log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
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
constexpr uint8_t kMaxBondOrientationalOrder3D = 12u;
constexpr size_t kSteinhardtOrderCount =
    size_t(kMaxBondOrientationalOrder3D - kMinBondOrientationalOrder + 1u);
constexpr size_t kStoredSteinhardtComponentCount =
    size_t((kMaxBondOrientationalOrder3D + 1u) * (kMaxBondOrientationalOrder3D + 2u) / 2u
           - 3u);
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
                    ParticleColorStatsCache &statsCache,
                    const ViewerState &viewerState)
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
    const std::vector<std::vector<NearestNeighborData>> *neighborLists =
        particleSystem.hasNeighborAnalysis() ? &particleSystem.neighborAnalysis() : nullptr;
    const bool useFrankKasperColoring =
        viewerState.frankKasperViewModeEnabled
        && colorMode == ColorMode::BondCount
        && patchMetadata != nullptr
        && neighborLists != nullptr;

    constexpr std::array<float, 4> kFrankKasperNeighbor12UnbondedColor = {
        0.93f, 0.80f, 0.28f, 1.0f
    };
    constexpr std::array<float, 4> kFrankKasperUnbondedColor = {
        0.30f, 0.30f, 0.34f, 1.0f
    };

    for (size_t index = 0; index < particles.size(); ++index)
    {
        Particle &particle = particles[index];
        if (useFrankKasperColoring && index < patchMetadata->size()
            && index < neighborLists->size())
        {
            const size_t fkBondCount = (*patchMetadata)[index].bondIds.size();
            const size_t neighborCount = (*neighborLists)[index].size();
            if (fkBondCount == 0u)
            {
                particle.color = neighborCount == 12u
                                     ? kFrankKasperNeighbor12UnbondedColor
                                     : kFrankKasperUnbondedColor;
            }
            else
            {
                particle.color = colorFromLetter(
                    static_cast<char>('A' + (fkBondCount % kParticlePaletteColorCount)));
            }
            continue;
        }

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
        cvt::log::info() << "No particles are currently selected for bond-order output."
                 << std::endl;
        return;
    }

    const uint8_t selectedOrder = clampedBondOrientationalOrder(viewerState);
    if (!particleSystem.hasAnalysisResults(selectedOrder))
    {
        cvt::log::info() << "Bond-order analysis is not available yet. Compute neighbors first."
                 << std::endl;
        return;
    }

    const std::vector<Particle> &particles = particleSystem.particles();
    const std::vector<ParticleAnalysisData> &analysisResults = particleSystem.analysisResults();
    const bool isTwoDimensional =
        viewerState.fileDimensionality == TrajectoryReader::Dimensionality::TwoDimensional;
    bool foundSelectedParticle = false;

    cvt::log::info() << std::fixed << std::setprecision(6)
                     << "Bond-order values for selected particles";
    if (isTwoDimensional)
    {
        cvt::log::info() << " (symmetry = " << unsigned(selectedOrder) << ')';
    }
    else
    {
        cvt::log::info() << " (l = " << unsigned(selectedOrder) << ')';
    }
    cvt::log::info() << ':' << std::endl;

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
        const uint32_t displayParticleId = particle.id > 0u ? (particle.id - 1u) : 0u;
        cvt::log::info() << "  Particle " << displayParticleId
                         << ": nearest neighbors=" << analysis.neighborCount;
        if (isTwoDimensional)
        {
            cvt::log::info() << ", |psi_" << unsigned(selectedOrder) << "|="
                             << analysis.bondOrientationalMagnitude
                             << ", phase=" << analysis.bondOrientationalPhase;
        }
        else
        {
            const size_t orderIndex = steinhardtArrayIndex(selectedOrder);
            cvt::log::info() << ", |q_" << unsigned(selectedOrder) << "|="
                             << analysis.steinhardtQValues[orderIndex]
                             << ", |qbar_" << unsigned(selectedOrder) << "|="
                             << analysis.steinhardtQBarValues[orderIndex];
        }
        cvt::log::info() << std::endl;
    }

    if (!foundSelectedParticle)
    {
        cvt::log::info() << "No selected particle IDs were found in the current frame."
                         << std::endl;
    }
}

void invalidateNeighborAnalysis(ViewerState &viewerState, ParticleSystem &particleSystem)
{
    viewerState.neighborAnalysisValid = false;
    viewerState.frankKasperBondsCached = false;
    viewerState.frankKasperViewModeEnabled = false;
    viewerState.twelveCoordinatedBondViewModeEnabled = false;
    viewerState.pendingActivateTwelveCoordinatedBondView = false;
    viewerState.pendingToggleFrankKasperUnbondedVisibility = false;
    viewerState.pendingRecalculateFrankKasperBonds = false;
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
        std::unordered_set<int> uniqueNeighborCells;
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
                    uniqueNeighborCells.insert(flattenedNeighborCell);
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
                    displacement = simulationBox.nearestImage(displacement);
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
                                    -displacement,
                                    distance);
            }
        }
    }
}

void calculateFrankKasperBonds(const ParticleSystem &particleSystem,
                               ParticleSystem &targetParticleSystem)
{
    if (!particleSystem.hasNeighborAnalysis())
    {
        return;
    }

    const std::vector<std::vector<NearestNeighborData>> &neighborLists =
        particleSystem.neighborAnalysis();
    const std::vector<Particle> &particles = particleSystem.particles();
    const size_t particleCount = particles.size();

    // Rebuild patch metadata with FK bond connectivity.
    targetParticleSystem.clearPatchyMetadata();
    for (size_t i = 0; i < particleCount; ++i)
    {
        PatchyParticleData patchData{};
        targetParticleSystem.addPatchyMetadata(patchData);
    }

    std::vector<PatchyParticleData> &patchyMetadata = targetParticleSystem.patchyMetadata();

    // Build a set of neighbors for each particle for efficient common-neighbor lookup.
    std::vector<std::unordered_set<uint32_t>> neighborSets(particleCount);
    for (size_t i = 0; i < particleCount; ++i)
    {
        for (const NearestNeighborData &neighbor : neighborLists[i])
        {
            neighborSets[i].insert(neighbor.neighborIndex);
        }
    }

    // Frank-Kasper criterion: bonded pair shares exactly 6 common neighbors.
    const uint32_t maxBondsPerParticle = 12u;
    std::vector<uint32_t> bondCountPerParticle(particleCount, 0u);
    for (size_t particleIndex = 0; particleIndex < particleCount; ++particleIndex)
    {
        if (neighborLists[particleIndex].empty())
        {
            continue;
        }

        for (const NearestNeighborData &neighbor : neighborLists[particleIndex])
        {
            const uint32_t neighborIndex = neighbor.neighborIndex;
            if (neighborIndex <= particleIndex)
            {
                // Process each unordered pair exactly once.
                continue;
            }

            if (neighborIndex >= neighborSets.size())
            {
                continue;
            }

            uint32_t commonNeighborCount = 0u;
            for (uint32_t commonNeighborIndex : neighborSets[particleIndex])
            {
                if (neighborSets[neighborIndex].contains(commonNeighborIndex))
                {
                    ++commonNeighborCount;
                }
            }

            if (commonNeighborCount == 6u)
            {
                if (bondCountPerParticle[particleIndex] >= maxBondsPerParticle
                    || bondCountPerParticle[neighborIndex] >= maxBondsPerParticle)
                {
                    continue;
                }

                int32_t bondId = static_cast<int32_t>(neighborIndex);
                patchyMetadata[particleIndex].bondIds.push_back(bondId);
                ++bondCountPerParticle[particleIndex];

                int32_t reverseBondId = static_cast<int32_t>(particleIndex);
                patchyMetadata[neighborIndex].bondIds.push_back(reverseBondId);
                ++bondCountPerParticle[neighborIndex];
            }
        }
    }
}

void calculateTwelveCoordinatedNeighborBonds(const ParticleSystem &particleSystem,
                                             ParticleSystem &targetParticleSystem)
{
    if (!particleSystem.hasNeighborAnalysis())
    {
        return;
    }

    const std::vector<std::vector<NearestNeighborData>> &neighborLists =
        particleSystem.neighborAnalysis();
    const std::vector<Particle> &particles = particleSystem.particles();
    const size_t particleCount = particles.size();

    targetParticleSystem.clearPatchyMetadata();
    for (size_t particleIndex = 0u; particleIndex < particleCount; ++particleIndex)
    {
        PatchyParticleData patchData{};
        targetParticleSystem.addPatchyMetadata(patchData);
    }

    std::vector<PatchyParticleData> &patchyMetadata = targetParticleSystem.patchyMetadata();

    std::vector<std::unordered_set<uint32_t>> neighborSets(particleCount);
    for (size_t particleIndex = 0u; particleIndex < particleCount; ++particleIndex)
    {
        for (const NearestNeighborData &neighbor : neighborLists[particleIndex])
        {
            neighborSets[particleIndex].insert(neighbor.neighborIndex);
        }
    }

    constexpr size_t kTargetCoordination = 12u;
    constexpr uint32_t kRequiredSharedNeighbors = 5u;
    for (size_t particleIndex = 0u; particleIndex < neighborLists.size(); ++particleIndex)
    {
        if (neighborLists[particleIndex].size() != kTargetCoordination)
        {
            continue;
        }

        for (const NearestNeighborData &neighbor : neighborLists[particleIndex])
        {
            const uint32_t neighborIndex = neighbor.neighborIndex;
            if (neighborIndex <= particleIndex || neighborIndex >= neighborLists.size())
            {
                continue;
            }

            if (neighborLists[neighborIndex].size() != kTargetCoordination)
            {
                continue;
            }

            uint32_t commonNeighborCount = 0u;
            for (uint32_t commonNeighborIndex : neighborSets[particleIndex])
            {
                if (neighborSets[neighborIndex].contains(commonNeighborIndex))
                {
                    ++commonNeighborCount;
                }
            }

            if (commonNeighborCount != kRequiredSharedNeighbors)
            {
                continue;
            }

            patchyMetadata[particleIndex].bondIds.push_back(static_cast<int32_t>(neighborIndex));
            patchyMetadata[neighborIndex].bondIds.push_back(static_cast<int32_t>(particleIndex));
        }
    }
}

void computeRadialDistributionFunction(ViewerState &viewerState,
                                       const SimulationBox &simulationBox,
                                       const ParticleSystem &particleSystem)
{
    const auto computeStart = std::chrono::steady_clock::now();

    const bool isTwoDimensional =
        viewerState.fileDimensionality == TrajectoryReader::Dimensionality::TwoDimensional;
    const int axisCount = isTwoDimensional ? 2 : 3;
    RdfBatchState &batch = viewerState.rdfBatchState;
    const bool requestedLowResMode = viewerState.rdfInteractionLowResActive;
    const bool lowResMode = batch.active ? batch.lowResMode : requestedLowResMode;
    const uint32_t requestedPairBudget = lowResMode
                                             ? viewerState.rdfLowResPairChecksPerStep
                                             : viewerState.rdfPairChecksPerStep;
    const uint64_t pairBudget = std::max<uint64_t>(requestedPairBudget, 10000u);
    const size_t requestedBinCount = std::clamp<size_t>(
        lowResMode ? std::min<uint16_t>(viewerState.rdfBinCount, viewerState.rdfLowResBinCount)
                   : viewerState.rdfBinCount,
        8u,
        512u);

    auto failAndClear = [&](const std::string &message) {
        viewerState.rdfStatusText = message;
        viewerState.rdfBinCenters.clear();
        viewerState.rdfValues.clear();
        viewerState.rdfPairCurves.clear();
        viewerState.rdfSampleParticleCount = 0u;
        viewerState.rdfComputedRadius = 0.0f;
        viewerState.rdfBinWidth = 0.0f;
        viewerState.rdfComputeMilliseconds = 0.0f;
        viewerState.rdfPendingCompute = false;
        viewerState.rdfDirty = false;
        viewerState.rdfBatchState = {};
    };

    const bool needBatchInit = !batch.active
                               || batch.dataRevision != viewerState.rdfDataRevision
                               || batch.binCount != requestedBinCount;
    if (needBatchInit)
    {
        batch = {};
        batch.active = true;
        batch.lowResMode = lowResMode;
        batch.dataRevision = viewerState.rdfDataRevision;
        batch.binCount = static_cast<uint16_t>(requestedBinCount);

        const std::vector<Particle> &particles = particleSystem.particles();
        batch.sampleIndices.reserve(particles.size());
        for (size_t particleIndex = 0u; particleIndex < particles.size(); ++particleIndex)
        {
            const Particle &particle = particles[particleIndex];
            if (viewerState.rdfUseVisibleParticlesOnly && !particle.visible)
            {
                continue;
            }

            const uint8_t typeIndex = particleTypeIndex(particle.typeLabel);
            if (typeIndex >= kParticlePaletteColorCount
                || !viewerState.rdfIncludedSpecies[typeIndex])
            {
                continue;
            }

            batch.sampleIndices.push_back(particleIndex);
            ++batch.typeCounts[typeIndex];
        }

        batch.sampleCount = batch.sampleIndices.size();
        viewerState.rdfSampleParticleCount = batch.sampleCount;
        if (batch.sampleCount < 2u)
        {
            failAndClear("Need at least two particles after filtering.");
            return;
        }

        const bx::Vec3 boxSize = simulationBox.size();
        batch.measure = simulationBox.measure(isTwoDimensional);
        if (batch.measure <= 1.0e-12)
        {
            failAndClear("RDF unavailable: simulation box measure is zero.");
            return;
        }

        float maxRadius = viewerState.rdfMaxRadius;
        bool usedNonPeriodicFallback = false;
        if (!(maxRadius > 0.0f))
        {
            float shortestPeriodicExtent = std::numeric_limits<float>::max();
            float shortestExtent = std::numeric_limits<float>::max();
            for (int axis = 0; axis < axisCount; ++axis)
            {
                const float axisExtent =
                    axis == 0 ? boxSize.x : (axis == 1 ? boxSize.y : boxSize.z);
                if (!(axisExtent > 0.0f))
                {
                    continue;
                }

                shortestExtent = bx::min(shortestExtent, axisExtent);
                if (simulationBox.isPeriodic(static_cast<size_t>(axis)))
                {
                    shortestPeriodicExtent = bx::min(shortestPeriodicExtent, axisExtent);
                }
            }

            if (shortestPeriodicExtent < std::numeric_limits<float>::max())
            {
                maxRadius = 0.5f * shortestPeriodicExtent;
            }
            else if (shortestExtent < std::numeric_limits<float>::max())
            {
                maxRadius = 0.5f * shortestExtent;
                usedNonPeriodicFallback = true;
            }
        }

        if (!(maxRadius > 1.0e-6f))
        {
            failAndClear("RDF unavailable: maximum radius is not positive.");
            return;
        }

        const float binWidth = maxRadius / static_cast<float>(batch.binCount);
        if (!(binWidth > 0.0f))
        {
            failAndClear("RDF unavailable: invalid bin width.");
            return;
        }

        batch.maxRadius = maxRadius;
        batch.binWidth = binWidth;
        batch.usedNonPeriodicFallback = usedNonPeriodicFallback;
        batch.pairCounts.assign(batch.binCount, 0.0);
        batch.pairCountsByType.clear();
        
        // Pre-compute shell measures for all bins (2D and 3D)
        batch.shellMeasures.resize(batch.binCount);
        for (size_t binIndex = 0u; binIndex < batch.binCount; ++binIndex)
        {
            const double radiusCenter = (static_cast<double>(binIndex) + 0.5)
                                        * static_cast<double>(batch.binWidth);
            batch.shellMeasures[binIndex] = isTwoDimensional
                                                ? (2.0 * double(bx::kPi) * radiusCenter
                                                   * static_cast<double>(batch.binWidth))
                                                : (4.0 * double(bx::kPi) * radiusCenter * radiusCenter
                                                   * static_cast<double>(batch.binWidth));
        }
        
        // Pre-allocate type-pair counters using active species only.
        // This avoids an O(sampleCount^2) scan during panel open.
        std::array<uint8_t, kParticlePaletteColorCount> activeTypes{};
        size_t activeTypeCount = 0u;
        for (uint8_t typeIndex = 0u; typeIndex < kParticlePaletteColorCount; ++typeIndex)
        {
            if (batch.typeCounts[typeIndex] > 0u)
            {
                activeTypes[activeTypeCount++] = typeIndex;
            }
        }

        batch.pairCountsByType.reserve(activeTypeCount * (activeTypeCount + 1u) / 2u);
        for (size_t i = 0u; i < activeTypeCount; ++i)
        {
            for (size_t j = i; j < activeTypeCount; ++j)
            {
                const uint8_t typeMin = activeTypes[i];
                const uint8_t typeMax = activeTypes[j];
                const uint16_t pairKey = static_cast<uint16_t>((typeMin << 8u) | typeMax);
                batch.pairCountsByType[pairKey].assign(batch.binCount, 0.0);
            }
        }
        
        batch.nextI = 0u;
        batch.nextJ = 1u;
        batch.processedPairChecks = 0u;
        batch.totalPairChecks = (static_cast<uint64_t>(batch.sampleCount)
                                 * static_cast<uint64_t>(batch.sampleCount - 1u))
                                / 2u;
        batch.accumulatedComputeMilliseconds = 0.0f;

        viewerState.rdfComputedRadius = batch.maxRadius;
        viewerState.rdfBinWidth = batch.binWidth;
    }

    const std::vector<Particle> &particles = particleSystem.particles();
    const bool sphericalBounds = simulationBox.shape() == SimulationBox::Shape::Spherical;

    uint64_t processedThisStep = 0u;
    while (processedThisStep < pairBudget && (batch.nextI + 1u) < batch.sampleCount)
    {
        const Particle &particleI = particles[batch.sampleIndices[batch.nextI]];
        const uint8_t typeI = particleTypeIndex(particleI.typeLabel);

        while (processedThisStep < pairBudget && batch.nextJ < batch.sampleCount)
        {
            const Particle &particleJ = particles[batch.sampleIndices[batch.nextJ]];
            const uint8_t typeJ = particleTypeIndex(particleJ.typeLabel);

            ++processedThisStep;
            ++batch.processedPairChecks;

            bx::Vec3 displacement = particleJ.position - particleI.position;
            if (isTwoDimensional)
            {
                displacement.z = 0.0f;
            }

            if (!sphericalBounds)
            {
                displacement = simulationBox.nearestImage(displacement);
            }

            // Consistent distance calculation using bx::length
            const float distance = bx::length(displacement);
            if (distance > 1.0e-6f && distance < batch.maxRadius)
            {
                const size_t binIndex = static_cast<size_t>(distance / batch.binWidth);
                const size_t clampedBinIndex = binIndex < batch.binCount ? binIndex : (batch.binCount - 1u);
                batch.pairCounts[clampedBinIndex] += 1.0;

                const uint8_t typeMin = bx::min(typeI, typeJ);
                const uint8_t typeMax = bx::max(typeI, typeJ);
                const uint16_t pairKey = static_cast<uint16_t>((typeMin << 8u) | typeMax);
                // Type-pair vectors are pre-allocated, no need to check/initialize
                auto it = batch.pairCountsByType.find(pairKey);
                if (it != batch.pairCountsByType.end())
                {
                    it->second[clampedBinIndex] += 1.0;
                }
            }

            ++batch.nextJ;
        }

        if (batch.nextJ >= batch.sampleCount)
        {
            ++batch.nextI;
            batch.nextJ = batch.nextI + 1u;
        }
    }

    const auto computeEnd = std::chrono::steady_clock::now();
    const float stepMilliseconds =
        std::chrono::duration<float, std::milli>(computeEnd - computeStart).count();
    batch.accumulatedComputeMilliseconds += stepMilliseconds;

    auto populateCurvesFromBatch = [&](double pairCountScale) {
        const double sampleDensity = static_cast<double>(batch.sampleCount) / batch.measure;
        viewerState.rdfBinCenters.resize(batch.binCount);
        viewerState.rdfValues.assign(batch.binCount, 0.0f);
        for (size_t binIndex = 0u; binIndex < batch.binCount; ++binIndex)
        {
            const double radiusCenter = (static_cast<double>(binIndex) + 0.5)
                                        * static_cast<double>(batch.binWidth);
            const double shellMeasure = batch.shellMeasures[binIndex];  // Use pre-computed
            viewerState.rdfBinCenters[binIndex] = static_cast<float>(radiusCenter);

            if (shellMeasure <= 0.0)
            {
                continue;
            }

            const double denominator =
                static_cast<double>(batch.sampleCount) * sampleDensity * shellMeasure;
            const double scaledPairCount = pairCountScale * batch.pairCounts[binIndex];
            viewerState.rdfValues[binIndex] = denominator > 0.0
                                                  ? static_cast<float>((2.0 * scaledPairCount)
                                                                       / denominator)
                                                  : 0.0f;
        }

        viewerState.rdfPairCurves.clear();
        viewerState.rdfPairCurves.reserve(batch.pairCountsByType.size());
        for (const auto &[pairKey, counts] : batch.pairCountsByType)
        {
            const uint8_t typeA = static_cast<uint8_t>((pairKey >> 8u) & 0xFFu);
            const uint8_t typeB = static_cast<uint8_t>(pairKey & 0xFFu);
            const size_t countA = batch.typeCounts[typeA];
            const size_t countB = batch.typeCounts[typeB];
            if (countA == 0u || countB == 0u)
            {
                continue;
            }

            RdfPairCurve curve{};
            curve.typeIndexA = typeA;
            curve.typeIndexB = typeB;
            curve.values.assign(batch.binCount, 0.0f);

            for (size_t binIndex = 0u; binIndex < batch.binCount; ++binIndex)
            {
                const double shellMeasure = batch.shellMeasures[binIndex];  // Use pre-computed
                if (shellMeasure <= 0.0)
                {
                    continue;
                }

                const double scaledPairCount = pairCountScale * counts[binIndex];
                const double numerator = (typeA == typeB)
                                             ? (2.0 * scaledPairCount)
                                             : scaledPairCount;
                const double densityTerm = (typeA == typeB)
                                               ? (static_cast<double>(countA)
                                                  * static_cast<double>(countA)
                                                  / batch.measure)
                                               : (static_cast<double>(countA)
                                                  * static_cast<double>(countB)
                                                  / batch.measure);
                const double denominator = densityTerm * shellMeasure;
                curve.values[binIndex] = denominator > 0.0
                                             ? static_cast<float>(numerator / denominator)
                                             : 0.0f;
            }

            viewerState.rdfPairCurves.push_back(std::move(curve));
        }

        std::sort(viewerState.rdfPairCurves.begin(), viewerState.rdfPairCurves.end(),
                  [](const RdfPairCurve &lhs, const RdfPairCurve &rhs) {
                      if (lhs.typeIndexA != rhs.typeIndexA)
                      {
                          return lhs.typeIndexA < rhs.typeIndexA;
                      }
                      return lhs.typeIndexB < rhs.typeIndexB;
                  });

        viewerState.rdfSampleParticleCount = batch.sampleCount;
        viewerState.rdfComputedRadius = batch.maxRadius;
        viewerState.rdfBinWidth = batch.binWidth;
    };

    const bool finished = (batch.nextI + 1u) >= batch.sampleCount;

    const double progressiveScale =
        (batch.processedPairChecks > 0u && batch.processedPairChecks < batch.totalPairChecks)
            ? (double(batch.totalPairChecks) / double(batch.processedPairChecks))
            : 1.0;
    populateCurvesFromBatch(progressiveScale);
    viewerState.rdfComputeMilliseconds = batch.accumulatedComputeMilliseconds;

    if (!finished)
    {
        std::ostringstream statusStr;
        statusStr << "RDF batching: " << batch.processedPairChecks
                  << "/" << batch.totalPairChecks
                  << " pairs (latest step " << stepMilliseconds
                  << " ms, showing progressive estimate).";
        viewerState.rdfStatusText = statusStr.str();
        viewerState.rdfPendingCompute = true;
        viewerState.rdfDirty = true;
        return;
    }

    if (batch.lowResMode)
    {
        std::ostringstream statusStr;
        statusStr << "Computed low-resolution interaction preview ("
                  << batch.processedPairChecks
                  << " pairs). Full-resolution recompute will run after interaction.";
        viewerState.rdfStatusText = statusStr.str();
        viewerState.rdfNeedsFullResolutionRefine = true;
    }
    else
    {
        viewerState.rdfStatusText = batch.usedNonPeriodicFallback
                                        ? "Computed with non-periodic fallback radius."
                                        : "Computed.";
        viewerState.rdfNeedsFullResolutionRefine = false;
    }

    viewerState.rdfPendingCompute = false;
    viewerState.rdfDirty = false;
    batch.active = false;
}
