#include "ParticleSystem.h"
#include "SimulationBox.h"
#include "TestSupport.h"
#include "TrajectoryReader.h"

#include <catch_amalgamated.hpp>

using Catch::Approx;

namespace
{

/// A particle system without a render type; enough for loading and analysis.
ParticleSystem makeParticleSystem()
{
    return ParticleSystem(nullptr);
}

} // namespace

TEST_CASE("Sphere files: frames, particles and box", "[TrajectoryReader]")
{
    const TemporaryFile file("two_frames.sph",
                             "2\n"
                             "10 10 10\n"
                             "A 1 2 3 0.5\n"
                             "B 4 5 6 0.75\n"
                             "# comment lines and blank lines are ignored\n"
                             "\n"
                             "& 1\n"
                             "8 9 10\n"
                             "C 7 8 9 1.25\n");

    const TrajectoryReader reader(file.path());
    REQUIRE(reader.isOpen());
    CHECK(reader.fileType() == TrajectoryReader::FileType::Sphere);
    CHECK(reader.dimensionality() == TrajectoryReader::Dimensionality::ThreeDimensional);
    REQUIRE(reader.frameCount() == 2u);

    ParticleSystem particleSystem = makeParticleSystem();
    SimulationBox box;

    REQUIRE(reader.loadFrame(0, particleSystem, box));
    REQUIRE(particleSystem.size() == 2u);
    const Particle &first = particleSystem.particles()[0];
    CHECK(first.typeLabel == 'A');
    CHECK(first.position.x == Approx(1.0f));
    CHECK(first.position.y == Approx(2.0f));
    CHECK(first.position.z == Approx(3.0f));
    CHECK(first.sizeParams[0] == Approx(0.5f));
    CHECK(particleSystem.particles()[1].typeLabel == 'B');
    CHECK(particleSystem.particles()[1].sizeParams[0] == Approx(0.75f));
    CHECK(box.size().x == Approx(10.0f));
    CHECK(box.isPeriodic(0));
    CHECK(box.isPeriodic(1));
    CHECK(box.isPeriodic(2));

    REQUIRE(reader.loadFrame(1, particleSystem, box));
    REQUIRE(particleSystem.size() == 1u);
    CHECK(particleSystem.particles()[0].typeLabel == 'C');
    CHECK(particleSystem.particles()[0].sizeParams[0] == Approx(1.25f));
    CHECK(box.size().z == Approx(10.0f));
}

TEST_CASE("Ordered sphere files keep per-particle order parameters", "[TrajectoryReader]")
{
    SECTION("Extra columns are stored in order")
    {
        const TemporaryFile file("ordered.osph",
                                 "2\n"
                                 "10 10 10\n"
                                 "A 1 1 1 0.5 0.25 -3\n"
                                 "A 2 2 2 0.5 0.75 4\n");
        const TrajectoryReader reader(file.path());
        REQUIRE(reader.isOpen());
        CHECK(reader.fileType() == TrajectoryReader::FileType::OrderedSphere);

        ParticleSystem particleSystem = makeParticleSystem();
        SimulationBox box;
        REQUIRE(reader.loadFrame(0, particleSystem, box));
        const std::vector<float> &first = particleSystem.particles()[0].orderParameters;
        const std::vector<float> &second = particleSystem.particles()[1].orderParameters;
        REQUIRE(first.size() == 2u);
        REQUIRE(second.size() == 2u);
        CHECK(first[0] == Approx(0.25f));
        CHECK(first[1] == Approx(-3.0f));
        CHECK(second[0] == Approx(0.75f));
        CHECK(second[1] == Approx(4.0f));
    }

    SECTION("A different number of columns per particle is rejected")
    {
        const TemporaryFile file("mismatched.osph",
                                 "2\n"
                                 "10 10 10\n"
                                 "A 1 1 1 0.5 0.25 -3\n"
                                 "A 2 2 2 0.5 0.75\n");
        const TrajectoryReader reader(file.path());
        ParticleSystem particleSystem = makeParticleSystem();
        SimulationBox box;
        if (reader.isOpen())
        {
            CHECK_FALSE(reader.loadFrame(0, particleSystem, box));
        }
        CHECK_FALSE(reader.error().empty());
    }
}

TEST_CASE("A truncated final frame is dropped, earlier frames still load", "[TrajectoryReader]")
{
    const TemporaryFile file("truncated.sph",
                             "1\n"
                             "10 10 10\n"
                             "A 1 1 1 0.5\n"
                             "1\n"
                             "10 10 10\n"
                             "A 2 2 2 0.5\n"
                             "3\n"
                             "10 10 10\n"
                             "A 3 3 3 0.5\n");

    const TrajectoryReader reader(file.path());
    REQUIRE(reader.isOpen());
    REQUIRE(reader.frameCount() == 2u);

    ParticleSystem particleSystem = makeParticleSystem();
    SimulationBox box;
    REQUIRE(reader.loadFrame(1, particleSystem, box));
    REQUIRE(particleSystem.size() == 1u);
    CHECK(particleSystem.particles()[0].position.x == Approx(2.0f));
}

TEST_CASE("Disk files are two-dimensional", "[TrajectoryReader]")
{
    const TemporaryFile file("disks.dsk",
                             "2\n"
                             "10 10 0\n"
                             "A 1 2 0.5\n"
                             "B 3 4 0.5\n");

    const TrajectoryReader reader(file.path());
    REQUIRE(reader.isOpen());
    CHECK(reader.fileType() == TrajectoryReader::FileType::Disk);
    CHECK(reader.dimensionality() == TrajectoryReader::Dimensionality::TwoDimensional);

    ParticleSystem particleSystem = makeParticleSystem();
    SimulationBox box;
    REQUIRE(reader.loadFrame(0, particleSystem, box));
    REQUIRE(particleSystem.size() == 2u);
    CHECK(particleSystem.particles()[1].position.x == Approx(3.0f));
    CHECK(particleSystem.particles()[1].position.y == Approx(4.0f));
    CHECK(particleSystem.particles()[1].position.z == Approx(0.0f));
}

TEST_CASE("LAMMPS dumps: bounds, types and diameters", "[TrajectoryReader]")
{
    const TemporaryFile file("dump.lammpstrj",
                             "ITEM: TIMESTEP\n"
                             "0\n"
                             "ITEM: NUMBER OF ATOMS\n"
                             "2\n"
                             "ITEM: BOX BOUNDS pp pp pp\n"
                             "-5 5\n"
                             "0 20\n"
                             "1 4\n"
                             "ITEM: ATOMS id type x y z diameter\n"
                             "1 1 0 1 2 1.0\n"
                             "2 2 1 2 3 3.0\n");

    const TrajectoryReader reader(file.path());
    REQUIRE(reader.isOpen());
    CHECK(reader.fileType() == TrajectoryReader::FileType::LammpsTrajectory);
    REQUIRE(reader.frameCount() == 1u);

    ParticleSystem particleSystem = makeParticleSystem();
    SimulationBox box;
    REQUIRE(reader.loadFrame(0, particleSystem, box));
    REQUIRE(particleSystem.size() == 2u);

    CHECK(box.minBounds().x == Approx(-5.0f));
    CHECK(box.maxBounds().x == Approx(5.0f));
    CHECK(box.size().y == Approx(20.0f));
    CHECK(box.size().z == Approx(3.0f));

    const Particle &second = particleSystem.particles()[1];
    CHECK(second.typeLabel == 'B');
    CHECK(second.sizeParams[0] == Approx(1.5f)); // diameter 3 -> radius 1.5
    CHECK(second.position.x == Approx(1.0f));
    CHECK(second.position.y == Approx(2.0f));
    CHECK(second.position.z == Approx(3.0f));
}

TEST_CASE("LAMMPS atom ids follow the internal id = index + 1 convention", "[TrajectoryReader]")
{
    // Internal ids are 1-based (0 means "no particle" in the pick buffer) and
    // are shown to the user as id - 1, so LAMMPS atom id N must become N + 1.
    const TemporaryFile file("zero_id.lammpstrj",
                             "ITEM: TIMESTEP\n"
                             "0\n"
                             "ITEM: NUMBER OF ATOMS\n"
                             "2\n"
                             "ITEM: BOX BOUNDS pp pp pp\n"
                             "0 10\n"
                             "0 10\n"
                             "0 10\n"
                             "ITEM: ATOMS id type x y z\n"
                             "0 1 1 1 1\n"
                             "5 1 2 2 2\n");

    const TrajectoryReader reader(file.path());
    REQUIRE(reader.isOpen());
    ParticleSystem particleSystem = makeParticleSystem();
    SimulationBox box;
    REQUIRE(reader.loadFrame(0, particleSystem, box));
    REQUIRE(particleSystem.size() == 2u);
    CHECK(particleSystem.particles()[0].id == 1u);
    CHECK(particleSystem.particles()[1].id == 6u);
}

TEST_CASE("Unknown extensions are rejected", "[TrajectoryReader]")
{
    const TemporaryFile file("particles.xyz", "1\n10 10 10\nA 1 1 1 0.5\n");
    const TrajectoryReader reader(file.path());
    CHECK_FALSE(reader.isOpen());
    CHECK_FALSE(reader.error().empty());
}
