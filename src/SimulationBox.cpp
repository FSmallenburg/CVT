#include "SimulationBox.h"
#include "BxVec3Operators.h"

#include <cmath>

namespace
{

float wrapAxis(float value, float minBound, float maxBound)
{
    float extent = maxBound - minBound;
    if (extent <= 0.0f)
    {
        return value;
    }

    while (value < minBound)
    {
        value += extent;
    }
    while (value > maxBound)
    {
        value -= extent;
    }
    return value;
}

float nearestAxis(float delta, float extent)
{
    if (extent <= 0.0f)
    {
        return delta;
    }

    float halfExtent = 0.5f * extent;
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

bx::Vec3 transformByMatrix(const std::array<float, 9> &matrix, const bx::Vec3 &value)
{
    return {
        matrix[0] * value.x + matrix[1] * value.y + matrix[2] * value.z,
        matrix[3] * value.x + matrix[4] * value.y + matrix[5] * value.z,
        matrix[6] * value.x + matrix[7] * value.y + matrix[8] * value.z,
    };
}

std::array<float, 9> inverseCellMatrix(const std::array<bx::Vec3, 3> &cellVectors)
{
    const float a00 = cellVectors[0].x;
    const float a01 = cellVectors[1].x;
    const float a02 = cellVectors[2].x;
    const float a10 = cellVectors[0].y;
    const float a11 = cellVectors[1].y;
    const float a12 = cellVectors[2].y;
    const float a20 = cellVectors[0].z;
    const float a21 = cellVectors[1].z;
    const float a22 = cellVectors[2].z;

    const float c00 = a11 * a22 - a12 * a21;
    const float c01 = -(a10 * a22 - a12 * a20);
    const float c02 = a10 * a21 - a11 * a20;
    const float c10 = -(a01 * a22 - a02 * a21);
    const float c11 = a00 * a22 - a02 * a20;
    const float c12 = -(a00 * a21 - a01 * a20);
    const float c20 = a01 * a12 - a02 * a11;
    const float c21 = -(a00 * a12 - a02 * a10);
    const float c22 = a00 * a11 - a01 * a10;

    const float determinant = a00 * c00 + a01 * c01 + a02 * c02;
    const float inverseDeterminant = determinant != 0.0f ? (1.0f / determinant) : 0.0f;
    return {
        c00 * inverseDeterminant, c10 * inverseDeterminant, c20 * inverseDeterminant,
        c01 * inverseDeterminant, c11 * inverseDeterminant, c21 * inverseDeterminant,
        c02 * inverseDeterminant, c12 * inverseDeterminant, c22 * inverseDeterminant,
    };
}

bx::Vec3 fractionalToCartesian(const bx::Vec3 &origin,
                               const std::array<bx::Vec3, 3> &cellVectors,
                               const bx::Vec3 &fractional)
{
    return origin
           + cellVectors[0] * fractional.x
           + cellVectors[1] * fractional.y
           + cellVectors[2] * fractional.z;
}

std::array<bx::Vec3, 8> cellCorners(const bx::Vec3 &origin,
                                    const std::array<bx::Vec3, 3> &cellVectors)
{
    return {
        origin,
        origin + cellVectors[0],
        origin + cellVectors[0] + cellVectors[1],
        origin + cellVectors[1],
        origin + cellVectors[2],
        origin + cellVectors[0] + cellVectors[2],
        origin + cellVectors[0] + cellVectors[1] + cellVectors[2],
        origin + cellVectors[1] + cellVectors[2],
    };
}

} // namespace

SimulationBox::SimulationBox(const bx::Vec3 &minBounds, const bx::Vec3 &maxBounds)
    : m_minBounds(minBounds), m_maxBounds(maxBounds)
{
    setBounds(minBounds, maxBounds);
}

void SimulationBox::setBounds(const bx::Vec3 &minBounds, const bx::Vec3 &maxBounds)
{
    m_renderRadius = 0.5f * bx::max(size().x, bx::max(size().y, size().z));
    m_minBounds = minBounds;
    m_maxBounds = maxBounds;
    m_shape = Shape::Rectangular;
    m_isTriclinic = false;
    m_cellOrigin = minBounds;
    const bx::Vec3 boxSize = m_maxBounds - m_minBounds;
    m_cellVectors = {
        bx::Vec3{boxSize.x, 0.0f, 0.0f},
        bx::Vec3{0.0f, boxSize.y, 0.0f},
        bx::Vec3{0.0f, 0.0f, boxSize.z},
    };
    m_inverseCellMatrix = inverseCellMatrix(m_cellVectors);
    m_renderRadius = 0.5f * bx::max(size().x, bx::max(size().y, size().z));
}

void SimulationBox::setTriclinicBounds(const bx::Vec3 &origin,
                                       const bx::Vec3 &a,
                                       const bx::Vec3 &b,
                                       const bx::Vec3 &c)
{
    m_shape = Shape::Rectangular;
    m_isTriclinic = true;
    m_cellOrigin = origin;
    m_cellVectors = {a, b, c};
    m_inverseCellMatrix = inverseCellMatrix(m_cellVectors);

    const std::array<bx::Vec3, 8> allCorners = cellCorners(m_cellOrigin, m_cellVectors);
    m_minBounds = allCorners[0];
    m_maxBounds = allCorners[0];
    for (const bx::Vec3 &corner : allCorners)
    {
        m_minBounds.x = bx::min(m_minBounds.x, corner.x);
        m_minBounds.y = bx::min(m_minBounds.y, corner.y);
        m_minBounds.z = bx::min(m_minBounds.z, corner.z);
        m_maxBounds.x = bx::max(m_maxBounds.x, corner.x);
        m_maxBounds.y = bx::max(m_maxBounds.y, corner.y);
        m_maxBounds.z = bx::max(m_maxBounds.z, corner.z);
    }

    const bx::Vec3 boundsSize = m_maxBounds - m_minBounds;
    m_renderRadius = 0.5f * bx::max(boundsSize.x, bx::max(boundsSize.y, boundsSize.z));
}

void SimulationBox::setSphericalBounds(const bx::Vec3 &center, float boundsRadius,
                                       float renderRadius)
{
    const bx::Vec3 halfExtent{boundsRadius, boundsRadius, boundsRadius};
    m_minBounds = center - halfExtent;
    m_maxBounds = center + halfExtent;
    m_shape = Shape::Spherical;
    m_isTriclinic = false;
    m_cellOrigin = m_minBounds;
    m_cellVectors = {
        bx::Vec3{m_maxBounds.x - m_minBounds.x, 0.0f, 0.0f},
        bx::Vec3{0.0f, m_maxBounds.y - m_minBounds.y, 0.0f},
        bx::Vec3{0.0f, 0.0f, m_maxBounds.z - m_minBounds.z},
    };
    m_inverseCellMatrix = inverseCellMatrix(m_cellVectors);
    m_renderRadius = renderRadius;
}

void SimulationBox::setPeriodic(bool x, bool y, bool z)
{
    m_periodic = {x, y, z};
}

const bx::Vec3 &SimulationBox::minBounds() const
{
    return m_minBounds;
}

const bx::Vec3 &SimulationBox::maxBounds() const
{
    return m_maxBounds;
}

bx::Vec3 SimulationBox::size() const
{
    return m_maxBounds - m_minBounds;
}

bx::Vec3 SimulationBox::center() const
{
    if (m_shape == Shape::Spherical)
    {
        return 0.5f * (m_minBounds + m_maxBounds);
    }

    return m_cellOrigin + 0.5f * (m_cellVectors[0] + m_cellVectors[1] + m_cellVectors[2]);
}

SimulationBox::Shape SimulationBox::shape() const
{
    return m_shape;
}

bool SimulationBox::isTriclinic() const
{
    return m_isTriclinic;
}

std::array<bx::Vec3, 8> SimulationBox::corners() const
{
    if (m_shape == Shape::Spherical)
    {
        const bx::Vec3 &minBounds = m_minBounds;
        const bx::Vec3 &maxBounds = m_maxBounds;
        return {
            bx::Vec3{minBounds.x, minBounds.y, minBounds.z},
            bx::Vec3{maxBounds.x, minBounds.y, minBounds.z},
            bx::Vec3{maxBounds.x, maxBounds.y, minBounds.z},
            bx::Vec3{minBounds.x, maxBounds.y, minBounds.z},
            bx::Vec3{minBounds.x, minBounds.y, maxBounds.z},
            bx::Vec3{maxBounds.x, minBounds.y, maxBounds.z},
            bx::Vec3{maxBounds.x, maxBounds.y, maxBounds.z},
            bx::Vec3{minBounds.x, maxBounds.y, maxBounds.z},
        };
    }

    return cellCorners(m_cellOrigin, m_cellVectors);
}

float SimulationBox::renderRadius() const
{
    return m_renderRadius;
}

double SimulationBox::measure(bool isTwoDimensional) const
{
    if (m_shape == Shape::Spherical)
    {
        const double radius = static_cast<double>(m_renderRadius);
        return isTwoDimensional
                   ? (double(bx::kPi) * radius * radius)
                   : ((4.0 / 3.0) * double(bx::kPi) * radius * radius * radius);
    }

    if (m_isTriclinic)
    {
        if (isTwoDimensional)
        {
            return static_cast<double>(bx::length(bx::cross(m_cellVectors[0], m_cellVectors[1])));
        }

        return std::abs(static_cast<double>(bx::dot(m_cellVectors[0],
                                                    bx::cross(m_cellVectors[1],
                                                              m_cellVectors[2]))));
    }

    const bx::Vec3 boxSize = size();
    return isTwoDimensional
               ? (double(boxSize.x) * double(boxSize.y))
               : (double(boxSize.x) * double(boxSize.y) * double(boxSize.z));
}

bool SimulationBox::isPeriodic(size_t axis) const
{
    return axis < m_periodic.size() ? m_periodic[axis] : false;
}

void SimulationBox::wrapPosition(bx::Vec3 &position) const
{
    if (m_isTriclinic)
    {
        bx::Vec3 fractional = transformByMatrix(m_inverseCellMatrix, position - m_cellOrigin);
        if (m_periodic[0])
        {
            fractional.x -= std::floor(fractional.x);
        }
        if (m_periodic[1])
        {
            fractional.y -= std::floor(fractional.y);
        }
        if (m_periodic[2])
        {
            fractional.z -= std::floor(fractional.z);
        }
        position = fractionalToCartesian(m_cellOrigin, m_cellVectors, fractional);
        return;
    }

    if (m_periodic[0])
    {
        position.x = wrapAxis(position.x, m_minBounds.x, m_maxBounds.x);
    }
    if (m_periodic[1])
    {
        position.y = wrapAxis(position.y, m_minBounds.y, m_maxBounds.y);
    }
    if (m_periodic[2])
    {
        position.z = wrapAxis(position.z, m_minBounds.z, m_maxBounds.z);
    }
}

bx::Vec3 SimulationBox::nearestImage(bx::Vec3 delta) const
{
    if (m_isTriclinic)
    {
        bx::Vec3 fractional = transformByMatrix(m_inverseCellMatrix, delta);
        if (m_periodic[0])
        {
            fractional.x -= std::round(fractional.x);
        }
        if (m_periodic[1])
        {
            fractional.y -= std::round(fractional.y);
        }
        if (m_periodic[2])
        {
            fractional.z -= std::round(fractional.z);
        }
        return fractionalToCartesian({0.0f, 0.0f, 0.0f}, m_cellVectors, fractional);
    }

    bx::Vec3 boxSize = size();
    if (m_periodic[0])
    {
        delta.x = nearestAxis(delta.x, boxSize.x);
    }
    if (m_periodic[1])
    {
        delta.y = nearestAxis(delta.y, boxSize.y);
    }
    if (m_periodic[2])
    {
        delta.z = nearestAxis(delta.z, boxSize.z);
    }
    return delta;
}