#include "AnalysisSupport.h"
#include "ParticleSystem.h"
#include "SimulationBox.h"
#include "ViewerSupport.h"

#include <catch_amalgamated.hpp>

#include <cmath>
#include <vector>

using Catch::Approx;

namespace
{

/// Index into the l = 2..12 arrays of ParticleAnalysisData.
constexpr size_t orderIndex(int l)
{
    return size_t(l - 2);
}

/// Adds particles of radius 0.5 at @p basis offsets (in units of the lattice
/// constant) repeated over an n x n x n block of cubic cells, and returns the
/// matching periodic box.
SimulationBox makeCubicCrystal(ParticleSystem &particleSystem,
                               const std::vector<bx::Vec3> &basis,
                               float latticeConstant, int cellsPerSide)
{
    uint32_t nextId = 1u;
    for (int i = 0; i < cellsPerSide; ++i)
    {
        for (int j = 0; j < cellsPerSide; ++j)
        {
            for (int k = 0; k < cellsPerSide; ++k)
            {
                for (const bx::Vec3 &offset : basis)
                {
                    Particle particle;
                    // Shift by a quarter cell so no particle sits on the box edge.
                    particle.position = {(float(i) + offset.x + 0.25f) * latticeConstant,
                                         (float(j) + offset.y + 0.25f) * latticeConstant,
                                         (float(k) + offset.z + 0.25f) * latticeConstant};
                    particle.setUniformScale(0.5f);
                    particle.id = nextId++;
                    particleSystem.addParticle(particle);
                }
            }
        }
    }

    const float boxLength = float(cellsPerSide) * latticeConstant;
    SimulationBox box({0.0f, 0.0f, 0.0f}, {boxLength, boxLength, boxLength});
    box.setPeriodic(true, true, true);
    return box;
}

void runNeighborAnalysis(ViewerState &viewerState, const SimulationBox &box,
                         ParticleSystem &particleSystem)
{
    findNearestNeighbors(viewerState, box, particleSystem);
    computeAnalysisResults(viewerState, particleSystem);
    REQUIRE(particleSystem.analysisResults().size() == particleSystem.size());
}

} // namespace

TEST_CASE("FCC crystal: 12 neighbors and textbook Steinhardt values", "[Analysis]")
{
    // Nearest-neighbor distance a / sqrt(2) = 1 matches the particle diameter,
    // so the default cutoff (1.3 x contact) keeps only the first shell.
    const std::vector<bx::Vec3> fccBasis = {
        {0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.0f}, {0.5f, 0.0f, 0.5f}, {0.0f, 0.5f, 0.5f}};
    ParticleSystem particleSystem(nullptr);
    const SimulationBox box = makeCubicCrystal(particleSystem, fccBasis, std::sqrt(2.0f), 4);
    REQUIRE(particleSystem.size() == 256u);

    ViewerState viewerState;
    viewerState.fileDimensionality = TrajectoryReader::Dimensionality::ThreeDimensional;
    runNeighborAnalysis(viewerState, box, particleSystem);

    for (const ParticleAnalysisData &analysis : particleSystem.analysisResults())
    {
        REQUIRE(analysis.neighborCount == 12u);
        CHECK(analysis.steinhardtQValues[orderIndex(4)] == Approx(0.19094).margin(1e-4));
        CHECK(analysis.steinhardtQValues[orderIndex(6)] == Approx(0.57452).margin(1e-4));
        // In a perfect crystal the averaged values equal the local ones.
        CHECK(analysis.steinhardtQBarValues[orderIndex(6)] == Approx(0.57452).margin(1e-4));
    }

    for (const std::vector<NearestNeighborData> &neighbors : particleSystem.neighborAnalysis())
    {
        for (const NearestNeighborData &neighbor : neighbors)
        {
            CHECK(neighbor.distance == Approx(1.0f).margin(1e-4));
        }
    }
}

TEST_CASE("Simple cubic crystal: 6 neighbors and textbook Steinhardt values", "[Analysis]")
{
    ParticleSystem particleSystem(nullptr);
    const SimulationBox box = makeCubicCrystal(particleSystem, {{0.0f, 0.0f, 0.0f}}, 1.0f, 5);

    ViewerState viewerState;
    viewerState.fileDimensionality = TrajectoryReader::Dimensionality::ThreeDimensional;
    runNeighborAnalysis(viewerState, box, particleSystem);

    for (const ParticleAnalysisData &analysis : particleSystem.analysisResults())
    {
        REQUIRE(analysis.neighborCount == 6u);
        CHECK(analysis.steinhardtQValues[orderIndex(4)] == Approx(0.76376).margin(1e-4));
        CHECK(analysis.steinhardtQValues[orderIndex(6)] == Approx(0.35355).margin(1e-4));
    }
}

TEST_CASE("Neighbors are found across periodic boundaries only when periodic", "[Analysis]")
{
    ParticleSystem particleSystem(nullptr);
    for (const float x : {0.25f, 9.75f})
    {
        Particle particle;
        particle.position = {x, 5.0f, 5.0f};
        particle.setUniformScale(0.5f);
        particleSystem.addParticle(particle);
    }

    ViewerState viewerState;
    viewerState.fileDimensionality = TrajectoryReader::Dimensionality::ThreeDimensional;

    SimulationBox box({0.0f, 0.0f, 0.0f}, {10.0f, 10.0f, 10.0f});
    box.setPeriodic(true, true, true);
    findNearestNeighbors(viewerState, box, particleSystem);
    REQUIRE(particleSystem.neighborAnalysis()[0].size() == 1u);
    CHECK(particleSystem.neighborAnalysis()[0][0].distance == Approx(0.5f));
    CHECK(particleSystem.neighborAnalysis()[0][0].displacement.x == Approx(-0.5f));

    box.setPeriodic(false, false, false);
    findNearestNeighbors(viewerState, box, particleSystem);
    CHECK(particleSystem.neighborAnalysis()[0].empty());
}

TEST_CASE("Hexagonal 2D crystal: 6 neighbors and |psi_6| = 1", "[Analysis]")
{
    constexpr int columns = 8;
    constexpr int rows = 8; // even, so the lattice tiles the periodic box
    const float rowSpacing = std::sqrt(3.0f) / 2.0f;

    ParticleSystem particleSystem(nullptr);
    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            Particle particle;
            particle.position = {float(column) + 0.5f * float(row % 2) + 0.25f,
                                 (float(row) + 0.5f) * rowSpacing, 0.0f};
            particle.setUniformScale(0.5f);
            particleSystem.addParticle(particle);
        }
    }

    SimulationBox box({0.0f, 0.0f, 0.0f}, {float(columns), float(rows) * rowSpacing, 0.0f});
    box.setPeriodic(true, true, false);

    ViewerState viewerState;
    viewerState.fileDimensionality = TrajectoryReader::Dimensionality::TwoDimensional;
    runNeighborAnalysis(viewerState, box, particleSystem);

    for (const ParticleAnalysisData &analysis : particleSystem.analysisResults())
    {
        REQUIRE(analysis.neighborCount == 6u);
        CHECK(analysis.bondOrientationalMagnitudes[orderIndex(6)] == Approx(1.0f).margin(1e-4));
        CHECK(analysis.bondOrientationalMagnitudes[orderIndex(4)] == Approx(0.0f).margin(1e-4));
    }
}
