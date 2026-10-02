#include "SimulationBox.h"

#include <catch_amalgamated.hpp>

using Catch::Approx;

TEST_CASE("Rectangular box size, center and measure", "[SimulationBox]")
{
    const SimulationBox box({-1.0f, 0.0f, 2.0f}, {3.0f, 5.0f, 8.0f});

    CHECK(box.size().x == Approx(4.0f));
    CHECK(box.size().y == Approx(5.0f));
    CHECK(box.size().z == Approx(6.0f));
    CHECK(box.center().x == Approx(1.0f));
    CHECK(box.center().y == Approx(2.5f));
    CHECK(box.center().z == Approx(5.0f));
    CHECK(box.measure(false) == Approx(120.0));
    CHECK(box.measure(true) == Approx(20.0));
    CHECK_FALSE(box.isTriclinic());
}

TEST_CASE("Minimum image in a periodic rectangular box", "[SimulationBox]")
{
    SimulationBox box({0.0f, 0.0f, 0.0f}, {10.0f, 10.0f, 10.0f});
    box.setPeriodic(true, true, true);

    const bx::Vec3 wrapped = box.nearestImage({9.0f, -6.0f, 4.0f});
    CHECK(wrapped.x == Approx(-1.0f));
    CHECK(wrapped.y == Approx(4.0f));
    CHECK(wrapped.z == Approx(4.0f));

    SECTION("Non-periodic axes are left alone")
    {
        box.setPeriodic(true, false, true);
        const bx::Vec3 partial = box.nearestImage({9.0f, -6.0f, 9.0f});
        CHECK(partial.x == Approx(-1.0f));
        CHECK(partial.y == Approx(-6.0f));
        CHECK(partial.z == Approx(-1.0f));
    }
}

TEST_CASE("Wrapping positions into a periodic rectangular box", "[SimulationBox]")
{
    SimulationBox box({-5.0f, -5.0f, -5.0f}, {5.0f, 5.0f, 5.0f});
    box.setPeriodic(true, true, false);

    bx::Vec3 position{6.0f, -12.0f, 7.0f};
    box.wrapPosition(position);
    CHECK(position.x == Approx(-4.0f));
    CHECK(position.y == Approx(-2.0f));
    CHECK(position.z == Approx(7.0f)); // z is not periodic

    bx::Vec3 inside{1.0f, 2.0f, 3.0f};
    box.wrapPosition(inside);
    CHECK(inside.x == Approx(1.0f));
    CHECK(inside.y == Approx(2.0f));
    CHECK(inside.z == Approx(3.0f));
}

TEST_CASE("Minimum image and wrapping in a triclinic box", "[SimulationBox]")
{
    SimulationBox box;
    box.setTriclinicBounds({0.0f, 0.0f, 0.0f},
                           {10.0f, 0.0f, 0.0f},
                           {3.0f, 10.0f, 0.0f},
                           {0.0f, 0.0f, 10.0f});
    box.setPeriodic(true, true, true);
    REQUIRE(box.isTriclinic());

    SECTION("A full cell vector maps to zero")
    {
        const bx::Vec3 image = box.nearestImage({3.0f, 10.0f, 0.0f});
        CHECK(image.x == Approx(0.0f).margin(1e-5));
        CHECK(image.y == Approx(0.0f).margin(1e-5));
        CHECK(image.z == Approx(0.0f).margin(1e-5));
    }

    SECTION("Displacements are shifted along the tilted cell vectors")
    {
        const bx::Vec3 image = box.nearestImage({1.0f, 9.0f, 0.0f});
        // (1, 9) - b = (-2, -1)
        CHECK(image.x == Approx(-2.0f));
        CHECK(image.y == Approx(-1.0f));
    }

    SECTION("Wrapping moves a point back into the cell")
    {
        // (1, 2, 5) shifted by a + b + c.
        bx::Vec3 position{1.0f + 10.0f + 3.0f, 2.0f + 10.0f, 5.0f + 10.0f};
        box.wrapPosition(position);
        CHECK(position.x == Approx(1.0f));
        CHECK(position.y == Approx(2.0f));
        CHECK(position.z == Approx(5.0f));
    }
}

TEST_CASE("Spherical boxes", "[SimulationBox]")
{
    SimulationBox box;
    box.setSphericalBounds({0.0f, 0.0f, 0.0f}, 5.0f, 4.0f);
    CHECK(box.shape() == SimulationBox::Shape::Spherical);
    CHECK(box.renderRadius() == Approx(4.0f));
}
