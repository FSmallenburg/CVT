#pragma once

#include <bx/math.h>

namespace bx
{

inline Vec3 operator+(const Vec3 &lhs, const Vec3 &rhs)
{
    return add(lhs, rhs);
}

inline Vec3 operator-(const Vec3 &lhs, const Vec3 &rhs)
{
    return sub(lhs, rhs);
}

inline Vec3 operator-(const Vec3 &value)
{
    return {-value.x, -value.y, -value.z};
}

inline Vec3 operator*(const Vec3 &lhs, float scalar)
{
    return mul(lhs, scalar);
}

inline Vec3 operator*(float scalar, const Vec3 &rhs)
{
    return mul(rhs, scalar);
}

inline Vec3 operator/(const Vec3 &value, float scalar)
{
    return mul(value, 1.0f / scalar);
}

inline Vec3 &operator+=(Vec3 &lhs, const Vec3 &rhs)
{
    lhs = lhs + rhs;
    return lhs;
}

inline Vec3 &operator-=(Vec3 &lhs, const Vec3 &rhs)
{
    lhs = lhs - rhs;
    return lhs;
}

inline Vec3 &operator*=(Vec3 &value, float scalar)
{
    value = value * scalar;
    return value;
}

inline Vec3 &operator/=(Vec3 &value, float scalar)
{
    value = value / scalar;
    return value;
}

} // namespace bx
