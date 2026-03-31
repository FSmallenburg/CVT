#pragma once

#include <bx/math.h>

#include <array>

class SimulationBox
{
  public:
    enum class Shape
    {
        Rectangular,
        Spherical,
    };

    SimulationBox() = default;
    SimulationBox(const bx::Vec3 &minBounds, const bx::Vec3 &maxBounds);

    void setBounds(const bx::Vec3 &minBounds, const bx::Vec3 &maxBounds);
    void setSphericalBounds(const bx::Vec3 &center, float boundsRadius, float renderRadius);
    void setPeriodic(bool x, bool y, bool z);

    const bx::Vec3 &minBounds() const;
    const bx::Vec3 &maxBounds() const;
    bx::Vec3 size() const;
    bx::Vec3 center() const;
    Shape shape() const;
    float renderRadius() const;

    void wrapPosition(bx::Vec3 &position) const;
    bx::Vec3 nearestImage(bx::Vec3 delta) const;

  private:
    bx::Vec3 m_minBounds{-1.0f, -1.0f, -1.0f};
    bx::Vec3 m_maxBounds{1.0f, 1.0f, 1.0f};
    std::array<bool, 3> m_periodic{false, false, false};
    Shape m_shape = Shape::Rectangular;
    float m_renderRadius = 1.0f;
};