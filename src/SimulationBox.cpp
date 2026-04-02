#include "SimulationBox.h"

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

} // namespace

SimulationBox::SimulationBox(const bx::Vec3 &minBounds, const bx::Vec3 &maxBounds)
    : m_minBounds(minBounds), m_maxBounds(maxBounds)
{
    m_renderRadius = 0.5f * bx::max(size().x, bx::max(size().y, size().z));
}

void SimulationBox::setBounds(const bx::Vec3 &minBounds, const bx::Vec3 &maxBounds)
{
    m_minBounds = minBounds;
    m_maxBounds = maxBounds;
    m_shape = Shape::Rectangular;
    m_renderRadius = 0.5f * bx::max(size().x, bx::max(size().y, size().z));
}

void SimulationBox::setSphericalBounds(const bx::Vec3 &center, float boundsRadius,
                                       float renderRadius)
{
    const bx::Vec3 halfExtent{boundsRadius, boundsRadius, boundsRadius};
    m_minBounds = {center.x - halfExtent.x, center.y - halfExtent.y, center.z - halfExtent.z};
    m_maxBounds = {center.x + halfExtent.x, center.y + halfExtent.y, center.z + halfExtent.z};
    m_shape = Shape::Spherical;
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
    return {
        m_maxBounds.x - m_minBounds.x,
        m_maxBounds.y - m_minBounds.y,
        m_maxBounds.z - m_minBounds.z,
    };
}

bx::Vec3 SimulationBox::center() const
{
    return {
        0.5f * (m_minBounds.x + m_maxBounds.x),
        0.5f * (m_minBounds.y + m_maxBounds.y),
        0.5f * (m_minBounds.z + m_maxBounds.z),
    };
}

SimulationBox::Shape SimulationBox::shape() const
{
    return m_shape;
}

float SimulationBox::renderRadius() const
{
    return m_renderRadius;
}

bool SimulationBox::isPeriodic(size_t axis) const
{
    return axis < m_periodic.size() ? m_periodic[axis] : false;
}

void SimulationBox::wrapPosition(bx::Vec3 &position) const
{
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