#pragma once

#include <bx/math.h>

#include <array>

/// Describes the spatial domain of a simulation: its boundaries, shape, and
/// which axes are periodic. Used for minimum-image calculations and for
/// rendering the box outline.
class SimulationBox
{
  public:
    /// The geometric shape of the bounding region.
    enum class Shape
    {
        Rectangular, ///< Axis-aligned box defined by minBounds / maxBounds.
        Spherical,   ///< Sphere defined by a center and a radius.
    };

    SimulationBox() = default;
    /// Constructs a rectangular box spanning [@p minBounds, @p maxBounds].
    SimulationBox(const bx::Vec3 &minBounds, const bx::Vec3 &maxBounds);

    /// Sets rectangular bounds and resets the shape to Rectangular.
    void setBounds(const bx::Vec3 &minBounds, const bx::Vec3 &maxBounds);
    /// Sets triclinic bounds using a cell origin and three cell vectors.
    void setTriclinicBounds(const bx::Vec3 &origin,
                const bx::Vec3 &a,
                const bx::Vec3 &b,
                const bx::Vec3 &c);
    /// Sets spherical bounds. @p boundsRadius is used for periodic wrapping;
    /// @p renderRadius controls the size of the sphere drawn on screen.
    void setSphericalBounds(const bx::Vec3 &center, float boundsRadius, float renderRadius);
    /// Enables or disables periodic boundary conditions for each axis independently.
    void setPeriodic(bool x, bool y, bool z);

    const bx::Vec3 &minBounds() const;
    const bx::Vec3 &maxBounds() const;
    /// Returns the axis-aligned extents (maxBounds - minBounds).
    bx::Vec3 size() const;
    /// Returns the geometric center of the box.
    bx::Vec3 center() const;
    Shape shape() const;
    /// Returns true when the rectangular box is stored as a skewed triclinic cell.
    bool isTriclinic() const;
    /// Returns the 8 corner positions of the current box/cell.
    std::array<bx::Vec3, 8> corners() const;
    /// Returns the render radius (only meaningful for Spherical boxes).
    float renderRadius() const;
    /// Returns the box measure for the requested dimensionality:
    /// area in 2D, volume in 3D.
    double measure(bool isTwoDimensional) const;
    /// Returns true if the given axis (0 = X, 1 = Y, 2 = Z) is periodic.
    bool isPeriodic(size_t axis) const;

    /// Wraps @p position into the primary periodic image in-place.
    void wrapPosition(bx::Vec3 &position) const;
    /// Returns the minimum-image displacement @p delta adjusted for periodic
    /// boundaries, i.e. the shortest equivalent vector.
    bx::Vec3 nearestImage(bx::Vec3 delta) const;

  private:
    bx::Vec3 m_minBounds{-1.0f, -1.0f, -1.0f};
    bx::Vec3 m_maxBounds{1.0f, 1.0f, 1.0f};
    std::array<bool, 3> m_periodic{false, false, false};
    Shape m_shape = Shape::Rectangular;
    float m_renderRadius = 1.0f;
    bool m_isTriclinic = false;
    bx::Vec3 m_cellOrigin{-1.0f, -1.0f, -1.0f};
    std::array<bx::Vec3, 3> m_cellVectors{
      bx::Vec3{2.0f, 0.0f, 0.0f},
      bx::Vec3{0.0f, 2.0f, 0.0f},
      bx::Vec3{0.0f, 0.0f, 2.0f},
    };
    std::array<float, 9> m_inverseCellMatrix{
      0.5f, 0.0f, 0.0f,
      0.0f, 0.5f, 0.0f,
      0.0f, 0.0f, 0.5f,
    };
};