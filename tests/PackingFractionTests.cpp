#include "AnalysisSupport.h"
#include "ParticleSystem.h"
#include "SimulationBox.h"
#include "TestSupport.h"
#include "TrajectoryReader.h"

#include <catch_amalgamated.hpp>

#include <string>

using Catch::Approx;

namespace
{

// Four particles of radius 0.5 in a 10 x 10 (x 10) box.
const double kExpected2D = 4.0 * bx::kPi * 0.25 / 100.0;
const double kExpected3D = 4.0 * (4.0 / 3.0) * bx::kPi * 0.125 / 1000.0;

/// Loads frame 0 of @p contents (written to @p fileName) and returns its
/// packing fraction, using the dimensionality the reader detected.
double packingFractionOf(const std::string &fileName, const std::string &contents)
{
    const TemporaryFile file(fileName, contents);
    const TrajectoryReader reader(file.path());
    REQUIRE(reader.isOpen());
    ParticleSystem particleSystem(nullptr);
    SimulationBox box;
    REQUIRE(reader.loadFrame(0, particleSystem, box));
    REQUIRE(particleSystem.size() == 4u);
    return computePackingFraction(particleSystem, box, reader.fileType(),
                                  reader.dimensionality());
}

std::string fourParticles(const std::string &boxLine, const std::string &lineTail,
                          bool threeDimensional)
{
    std::string contents = "4\n" + boxLine + "\n";
    for (int i = 0; i < 4; ++i)
    {
        const std::string xy = std::to_string(2 * i + 1) + " 5";
        contents += "A " + xy + (threeDimensional ? " 5 " : " ") + lineTail + "\n";
    }
    return contents;
}

const std::string kIdentity = "1 0 0 0 1 0 0 0 1";

} // namespace

TEST_CASE("Packing fraction of spheres and disks", "[Analysis][PackingFraction]")
{
    CHECK(packingFractionOf("spheres.sph", fourParticles("10 10 10", "0.5", true))
          == Approx(kExpected3D));
    CHECK(packingFractionOf("disks.dsk", fourParticles("10 10 0", "0.5", false))
          == Approx(kExpected2D));
    CHECK(packingFractionOf("disks.gon", fourParticles("10 10 0", "0.5 0 0", false))
          == Approx(kExpected2D));
}

TEST_CASE("Packing fraction of patchy particles uses area in 2D", "[Analysis][PackingFraction]")
{
    SECTION(".ptc in a 3D box counts sphere volumes")
    {
        CHECK(packingFractionOf("patchy3d.ptc",
                                fourParticles("10 10 10", "0.5 0.95 1.12 " + kIdentity + " -1 -1",
                                              true))
              == Approx(kExpected3D));
    }

    SECTION(".ptc in a 2D box (two box lengths) counts disk areas")
    {
        // In 2D, .ptc lines use the .patch layout: x y, then a rotation angle.
        CHECK(packingFractionOf("patchy2d.ptc",
                                fourParticles("10 10", "0.5 0.95 1.12 0 -1 -1", false))
              == Approx(kExpected2D));
    }

    SECTION(".patch files count disk areas")
    {
        CHECK(packingFractionOf("patchy.patch",
                                fourParticles("10 10 1", "0.5 0.95 1.12 0 -1 -1 -1 -1", false))
              == Approx(kExpected2D));
    }
}
