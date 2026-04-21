#include "BondOrderScatter.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{

constexpr uint8_t kMinBondOrder = 2u;

float bondOrderComponentValue(const ParticleAnalysisData &analysis,
                              bool isTwoDimensional,
                              bool useAveragedValues,
                              size_t orderIndex)
{
    if (isTwoDimensional)
    {
        return analysis.bondOrientationalMagnitudes[orderIndex];
    }

    return useAveragedValues ? analysis.steinhardtQBarValues[orderIndex]
                             : analysis.steinhardtQValues[orderIndex];
}

double dotProduct(const std::vector<double> &first, const std::vector<double> &second)
{
    double sum = 0.0;
    for (size_t index = 0u; index < first.size() && index < second.size(); ++index)
    {
        sum += first[index] * second[index];
    }
    return sum;
}

double vectorLength(const std::vector<double> &values)
{
    return std::sqrt(dotProduct(values, values));
}

std::vector<double> multiplyMatrixVector(const std::vector<double> &matrix,
                                         size_t dimension,
                                         const std::vector<double> &vector)
{
    std::vector<double> result(dimension, 0.0);
    for (size_t row = 0u; row < dimension; ++row)
    {
        for (size_t col = 0u; col < dimension; ++col)
        {
            result[row] += matrix[row * dimension + col] * vector[col];
        }
    }
    return result;
}

std::vector<double> dominantEigenvector(const std::vector<double> &matrix,
                                        size_t dimension,
                                        std::vector<double> initialVector = {})
{
    if (dimension == 0u)
    {
        return {};
    }

    if (initialVector.size() != dimension)
    {
        initialVector.resize(dimension, 1.0);
        for (size_t index = 0u; index < dimension; ++index)
        {
            initialVector[index] = double(index + 1u);
        }
    }

    double length = vectorLength(initialVector);
    if (length <= 1.0e-12)
    {
        initialVector.assign(dimension, 0.0);
        initialVector[0] = 1.0;
    }
    else
    {
        for (double &value : initialVector)
        {
            value /= length;
        }
    }

    for (int iteration = 0; iteration < 32; ++iteration)
    {
        std::vector<double> nextVector =
            multiplyMatrixVector(matrix, dimension, initialVector);
        length = vectorLength(nextVector);
        if (length <= 1.0e-12)
        {
            break;
        }

        for (double &value : nextVector)
        {
            value /= length;
        }
        initialVector = nextVector;
    }

    return initialVector;
}

BondOrderScatterData buildBondOrderScatterData(
    const ParticleSystem &particleSystem,
    const std::array<bool, kParticlePaletteColorCount> &enabledSpecies,
    bool isTwoDimensional,
    BondOrderScatterMode scatterMode,
    uint8_t xOrder,
    uint8_t yOrder)
{
    BondOrderScatterData data;
    const std::vector<Particle> &particles = particleSystem.particles();
    const std::vector<ParticleAnalysisData> &analysisResults =
        particleSystem.analysisResults();
    const size_t particleCapacity = std::min(particles.size(), analysisResults.size());
    std::vector<size_t> includedParticleIndices;
    includedParticleIndices.reserve(particleCapacity);
    for (size_t particleIndex = 0u; particleIndex < particleCapacity; ++particleIndex)
    {
        const uint8_t typeIndex = particleTypeIndex(particles[particleIndex].typeLabel);
        if (typeIndex < enabledSpecies.size() && enabledSpecies[typeIndex])
        {
            includedParticleIndices.push_back(particleIndex);
        }
    }
    const size_t particleCount = includedParticleIndices.size();

    data.xValues.reserve(particleCount);
    data.yValues.reserve(particleCount);
    data.pointColors.reserve(particleCount);
    data.particleIds.reserve(particleCount);

    const bool usePcaMode = bondOrderScatterModeUsesPca(scatterMode);
    const bool useAveragedValues = bondOrderScatterModeUsesAveragedValues(scatterMode);

    if (!usePcaMode)
    {
        const size_t xOrderIndex = size_t(xOrder - kMinBondOrder);
        const size_t yOrderIndex = size_t(yOrder - kMinBondOrder);
        for (size_t particleIndex : includedParticleIndices)
        {
            const Particle &particle = particles[particleIndex];
            const ParticleAnalysisData &analysis = analysisResults[particleIndex];
            data.xValues.push_back(
                bondOrderComponentValue(analysis, isTwoDimensional, useAveragedValues,
                                        xOrderIndex));
            data.yValues.push_back(
                bondOrderComponentValue(analysis, isTwoDimensional, useAveragedValues,
                                        yOrderIndex));
            data.pointColors.push_back(
                ImGui::ColorConvertFloat4ToU32(ImVec4(particle.baseColor[0],
                                                      particle.baseColor[1],
                                                      particle.baseColor[2],
                                                      particle.baseColor[3])));
            data.particleIds.push_back(particle.id);
        }
        return data;
    }

    if (particleCount == 0u)
    {
        return data;
    }

    const uint8_t maximumBondOrder = isTwoDimensional ? 6u : 12u;
    const size_t featureCount = size_t(maximumBondOrder - kMinBondOrder + 1u);
    std::vector<double> means(featureCount, 0.0);
    std::vector<double> centeredFeatures(particleCount * featureCount, 0.0);

    for (size_t includedIndex = 0u; includedIndex < particleCount; ++includedIndex)
    {
        const size_t particleIndex = includedParticleIndices[includedIndex];
        const ParticleAnalysisData &analysis = analysisResults[particleIndex];
        for (size_t featureIndex = 0u; featureIndex < featureCount; ++featureIndex)
        {
            const double value =
                double(bondOrderComponentValue(analysis, isTwoDimensional,
                                               useAveragedValues, featureIndex));
            centeredFeatures[includedIndex * featureCount + featureIndex] = value;
            means[featureIndex] += value;
        }
    }

    const double inverseParticleCount = 1.0 / double(particleCount);
    for (double &mean : means)
    {
        mean *= inverseParticleCount;
    }

    for (size_t includedIndex = 0u; includedIndex < particleCount; ++includedIndex)
    {
        for (size_t featureIndex = 0u; featureIndex < featureCount; ++featureIndex)
        {
            centeredFeatures[includedIndex * featureCount + featureIndex] -= means[featureIndex];
        }
    }

    std::vector<double> covariance(featureCount * featureCount, 0.0);
    const double normalization = particleCount > 1u ? 1.0 / double(particleCount - 1u) : 1.0;
    for (size_t includedIndex = 0u; includedIndex < particleCount; ++includedIndex)
    {
        for (size_t row = 0u; row < featureCount; ++row)
        {
            const double rowValue = centeredFeatures[includedIndex * featureCount + row];
            for (size_t col = 0u; col < featureCount; ++col)
            {
                covariance[row * featureCount + col] +=
                    rowValue * centeredFeatures[includedIndex * featureCount + col]
                    * normalization;
            }
        }
    }

    std::vector<double> firstEigenvector = dominantEigenvector(covariance, featureCount);
    const std::vector<double> firstProjection =
        multiplyMatrixVector(covariance, featureCount, firstEigenvector);
    const double firstEigenvalue = dotProduct(firstEigenvector, firstProjection);

    std::vector<double> deflatedCovariance = covariance;
    for (size_t row = 0u; row < featureCount; ++row)
    {
        for (size_t col = 0u; col < featureCount; ++col)
        {
            deflatedCovariance[row * featureCount + col] -=
                firstEigenvalue * firstEigenvector[row] * firstEigenvector[col];
        }
    }

    std::vector<double> secondInitialVector(featureCount, 0.0);
    secondInitialVector[featureCount > 1u ? 1u : 0u] = 1.0;
    std::vector<double> secondEigenvector = dominantEigenvector(deflatedCovariance,
                                                                featureCount,
                                                                secondInitialVector);

    for (size_t includedIndex = 0u; includedIndex < particleCount; ++includedIndex)
    {
        double projectedX = 0.0;
        double projectedY = 0.0;
        for (size_t featureIndex = 0u; featureIndex < featureCount; ++featureIndex)
        {
            const double centeredValue =
                centeredFeatures[includedIndex * featureCount + featureIndex];
            projectedX += centeredValue * firstEigenvector[featureIndex];
            projectedY += centeredValue * secondEigenvector[featureIndex];
        }

        const size_t particleIndex = includedParticleIndices[includedIndex];
        const Particle &particle = particles[particleIndex];
        data.xValues.push_back(float(projectedX));
        data.yValues.push_back(float(projectedY));
        data.pointColors.push_back(
            ImGui::ColorConvertFloat4ToU32(ImVec4(particle.baseColor[0],
                                                  particle.baseColor[1],
                                                  particle.baseColor[2],
                                                  particle.baseColor[3])));
        data.particleIds.push_back(particle.id);
    }

    return data;
}

bool cacheMatches(const BondOrderScatterCache &cache, const ViewerState &viewerState)
{
    return cache.valid
           && cache.mode == viewerState.bondOrderScatterMode
           && cache.dimensionality == viewerState.fileDimensionality
           && cache.xOrder == viewerState.bondOrderScatterXAxisOrder
           && cache.yOrder == viewerState.bondOrderScatterYAxisOrder
           && cache.dataRevision == viewerState.bondOrderScatterDataRevision
           && cache.enabledSpecies == viewerState.bondOrderScatterTypeEnabled;
}

} // namespace

bool bondOrderScatterModeUsesPca(BondOrderScatterMode scatterMode)
{
    switch (scatterMode)
    {
    case BondOrderScatterMode::RawAxesQ:
    case BondOrderScatterMode::RawAxesQBar:
        return false;
    case BondOrderScatterMode::PrincipalComponentsQ:
    case BondOrderScatterMode::PrincipalComponentsQBar:
        return true;
    }

    return false;
}

bool bondOrderScatterModeUsesAveragedValues(BondOrderScatterMode scatterMode)
{
    switch (scatterMode)
    {
    case BondOrderScatterMode::RawAxesQBar:
    case BondOrderScatterMode::PrincipalComponentsQBar:
        return true;
    case BondOrderScatterMode::RawAxesQ:
    case BondOrderScatterMode::PrincipalComponentsQ:
        return false;
    }

    return false;
}

std::string bondOrderAxisLabel(bool isTwoDimensional,
                               uint8_t order,
                               bool useAveragedValues)
{
    if (isTwoDimensional)
    {
        return "|psi_" + std::to_string(unsigned(order)) + "|";
    }

    return (useAveragedValues ? "|qbar_" : "|q_")
           + std::to_string(unsigned(order)) + "|";
}

std::string bondOrderPcaSourceLabel(bool isTwoDimensional,
                                    bool useAveragedValues)
{
    if (isTwoDimensional)
    {
        return "PCA is computed from the |psi_l| values for l = 2..6.";
    }

    return useAveragedValues
               ? "PCA is computed from the |qbar_l| values for l = 2..12."
               : "PCA is computed from the |q_l| values for l = 2..12.";
}

BondOrderScatterData &getBondOrderScatterData(const ParticleSystem &particleSystem,
                                              ViewerState &viewerState)
{
    BondOrderScatterCache &cache = viewerState.bondOrderScatterCache;
    if (!cacheMatches(cache, viewerState))
    {
        cache.data = buildBondOrderScatterData(particleSystem,
                                               viewerState.bondOrderScatterTypeEnabled,
                                               viewerState.fileDimensionality
                                                   == TrajectoryReader::Dimensionality::TwoDimensional,
                                               viewerState.bondOrderScatterMode,
                                               viewerState.bondOrderScatterXAxisOrder,
                                               viewerState.bondOrderScatterYAxisOrder);
        cache.valid = true;
        cache.mode = viewerState.bondOrderScatterMode;
        cache.dimensionality = viewerState.fileDimensionality;
        cache.xOrder = viewerState.bondOrderScatterXAxisOrder;
        cache.yOrder = viewerState.bondOrderScatterYAxisOrder;
        cache.dataRevision = viewerState.bondOrderScatterDataRevision;
        cache.enabledSpecies = viewerState.bondOrderScatterTypeEnabled;
    }

    return cache.data;
}
