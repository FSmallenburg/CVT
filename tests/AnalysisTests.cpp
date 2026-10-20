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

TEST_CASE("Polygon orientation colors respect n-fold symmetry", "[Analysis][Color]")
{
    const float pentagonPeriod = 2.0f * bx::kPi / 5.0f;

    SECTION("Angles one symmetry period apart get the same color when folding")
    {
        const auto base = planarOrientationColor(0.3f, 5u, true);
        const auto shifted = planarOrientationColor(0.3f + 2.0f * pentagonPeriod, 5u, true);
        const auto negative = planarOrientationColor(0.3f - pentagonPeriod, 5u, true);
        for (int channel = 0; channel < 3; ++channel)
        {
            CHECK(shifted[channel] == Approx(base[channel]).margin(1e-4));
            CHECK(negative[channel] == Approx(base[channel]).margin(1e-4));
        }
    }

    SECTION("Without folding, the same angles get different colors")
    {
        const auto base = planarOrientationColor(0.3f, 5u, false);
        const auto shifted = planarOrientationColor(0.3f + 2.0f * pentagonPeriod, 5u, false);
        CHECK_FALSE((shifted[0] == Approx(base[0]).margin(1e-3)
                     && shifted[1] == Approx(base[1]).margin(1e-3)
                     && shifted[2] == Approx(base[2]).margin(1e-3)));
    }

    SECTION("Angle 0 is red; half a period is the opposite hue (cyan)")
    {
        const auto zero = planarOrientationColor(0.0f, 6u, true);
        CHECK(zero[0] == Approx(1.0f));
        CHECK(zero[1] == Approx(0.0f));
        CHECK(zero[2] == Approx(0.0f));

        const auto half = planarOrientationColor(0.5f * 2.0f * bx::kPi / 6.0f, 6u, true);
        CHECK(half[0] == Approx(0.0f).margin(1e-4));
        CHECK(half[1] == Approx(1.0f).margin(1e-4));
        CHECK(half[2] == Approx(1.0f).margin(1e-4));
    }

    SECTION("Disks are gray")
    {
        const auto disk = planarOrientationColor(1.0f, 0u, true);
        CHECK(disk[0] == Approx(disk[1]));
        CHECK(disk[1] == Approx(disk[2]));
    }
}

TEST_CASE("Orientation color mode colors polygons by folded angle", "[Analysis][Color]")
{
    ParticleSystem particleSystem(nullptr);
    const float hexagonPeriod = 2.0f * bx::kPi / 6.0f;
    for (const float angle : {0.2f, 0.2f + hexagonPeriod})
    {
        Particle particle;
        particle.direction = {std::cos(angle), std::sin(angle), 0.0f};
        particle.sizeParams[1] = 6.0f;
        particleSystem.addParticle(particle);
    }

    ViewerState viewerState;
    ParticleColorStatsCache statsCache;

    viewerState.orientationHistogramReduceBySymmetry = true;
    applyColorMode(particleSystem, ColorMode::Orientation, OrientationColorStyle::PolygonAngle,
                   false, statsCache, viewerState);
    const auto &first = particleSystem.particles()[0].color;
    const auto &second = particleSystem.particles()[1].color;
    for (int channel = 0; channel < 3; ++channel)
    {
        CHECK(second[channel] == Approx(first[channel]).margin(1e-4));
    }

    viewerState.orientationHistogramReduceBySymmetry = false;
    applyColorMode(particleSystem, ColorMode::Orientation, OrientationColorStyle::PolygonAngle,
                   false, statsCache, viewerState);
    CHECK_FALSE((particleSystem.particles()[1].color[0] == Approx(particleSystem.particles()[0].color[0]).margin(1e-3)
                 && particleSystem.particles()[1].color[1] == Approx(particleSystem.particles()[0].color[1]).margin(1e-3)
                 && particleSystem.particles()[1].color[2] == Approx(particleSystem.particles()[0].color[2]).margin(1e-3)));
}

TEST_CASE("Orientation color mode colors 2D patchy particles by patch symmetry",
          "[Analysis][Color]")
{
    // Three in-plane patches -> 3-fold symmetry; a 3D particle gets no angle color.
    const float period = 2.0f * bx::kPi / 3.0f;
    ParticleSystem particleSystem(nullptr);
    const auto addPatchy = [&](float angle, bool planar) {
        Particle particle;
        particleSystem.addParticle(particle);
        PatchyParticleData patchData;
        patchData.orientationMatrix = {std::cos(angle), -std::sin(angle), 0.0f,
                                       std::sin(angle), std::cos(angle), 0.0f,
                                       0.0f, 0.0f, 1.0f};
        patchData.planarPlacement = planar;
        patchData.bondIds = {-1, -1, -1};
        particleSystem.addPatchyMetadata(patchData);
    };
    addPatchy(0.4f, true);
    addPatchy(0.4f + period, true);
    addPatchy(0.4f, false);

    ViewerState viewerState;
    viewerState.orientationHistogramReduceBySymmetry = true;
    ParticleColorStatsCache statsCache;
    applyColorMode(particleSystem, ColorMode::Orientation,
                   OrientationColorStyle::PatchyPlanarAngle, false, statsCache, viewerState);

    const auto &particles = particleSystem.particles();
    const auto expected = planarOrientationColor(0.4f, 3u, true);
    for (int channel = 0; channel < 3; ++channel)
    {
        CHECK(particles[0].color[channel] == Approx(expected[channel]).margin(1e-4));
        CHECK(particles[1].color[channel] == Approx(expected[channel]).margin(1e-4));
    }
    // The non-planar particle falls back to the single default color.
    CHECK_FALSE((particles[2].color[0] == Approx(expected[0]).margin(1e-3)
                 && particles[2].color[1] == Approx(expected[1]).margin(1e-3)
                 && particles[2].color[2] == Approx(expected[2]).margin(1e-3)));
}
