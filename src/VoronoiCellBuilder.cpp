#include "VoronoiCellBuilder.h"
#include "BxVec3Operators.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{

struct Plane
{
    bx::Vec3 normal{0.0f, 0.0f, 1.0f};
    float offset = 0.0f;
};

constexpr float kEpsilon = 1.0e-5f;

float lengthSq(const bx::Vec3 &v)
{
    return bx::dot(v, v);
}

bool nearlyEqual(const bx::Vec3 &a, const bx::Vec3 &b, float epsilon)
{
    return lengthSq(a - b) <= epsilon * epsilon;
}

bool solvePlaneIntersection(const Plane &a, const Plane &b, const Plane &c, bx::Vec3 &outPoint)
{
    const bx::Vec3 bxcn = bx::cross(b.normal, c.normal);
    const float determinant = bx::dot(a.normal, bxcn);
    if (std::abs(determinant) <= kEpsilon)
    {
        return false;
    }

    const bx::Vec3 termA = bxcn * a.offset;
    const bx::Vec3 termB = bx::cross(c.normal, a.normal) * b.offset;
    const bx::Vec3 termC = bx::cross(a.normal, b.normal) * c.offset;
    outPoint = (termA + termB + termC) / determinant;
    return true;
}

bool isInsideAllPlanes(const bx::Vec3 &point, const std::vector<Plane> &planes)
{
    for (const Plane &plane : planes)
    {
        if (bx::dot(plane.normal, point) > plane.offset + 2.0f * kEpsilon)
        {
            return false;
        }
    }
    return true;
}

bx::Vec3 pickPerpendicular(const bx::Vec3 &normal)
{
    const bx::Vec3 axis = (std::abs(normal.x) < 0.8f) ? bx::Vec3{1.0f, 0.0f, 0.0f}
                                                       : bx::Vec3{0.0f, 1.0f, 0.0f};
    const bx::Vec3 tangent = bx::cross(axis, normal);
    const float len = bx::length(tangent);
    if (len <= kEpsilon)
    {
        return {0.0f, 0.0f, 1.0f};
    }
    return tangent * (1.0f / len);
}

void appendTriangle(const bx::Vec3 &a,
                    const bx::Vec3 &b,
                    const bx::Vec3 &c,
                    const bx::Vec3 &normal,
                    std::vector<PosNormalVertex> &vertices,
                    std::vector<uint16_t> &indices)
{
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    if (base > std::numeric_limits<uint16_t>::max() - 3u)
    {
        return;
    }

    vertices.push_back({a.x, a.y, a.z, normal.x, normal.y, normal.z});
    vertices.push_back({b.x, b.y, b.z, normal.x, normal.y, normal.z});
    vertices.push_back({c.x, c.y, c.z, normal.x, normal.y, normal.z});
    indices.push_back(static_cast<uint16_t>(base));
    indices.push_back(static_cast<uint16_t>(base + 1u));
    indices.push_back(static_cast<uint16_t>(base + 2u));
}

} // namespace

bool buildVoronoiCellMesh(const std::vector<bx::Vec3> &sites,
                          VoronoiMeshData &outMesh,
                          std::string &error)
{
    outMesh.vertices.clear();
    outMesh.indices.clear();
    error.clear();

    if (sites.size() < 4u)
    {
        error = "Voronoi point set requires at least 4 points";
        return false;
    }

    std::vector<Plane> planes;
    planes.reserve(sites.size());
    for (const bx::Vec3 &site : sites)
    {
        const float siteLengthSq = lengthSq(site);
        if (siteLengthSq <= kEpsilon)
        {
            error = "Voronoi point set contains a point at the origin";
            return false;
        }

        planes.push_back(Plane{site, 0.5f * siteLengthSq});
    }

    std::vector<bx::Vec3> uniqueVertices;
    for (size_t i = 0u; i + 2u < planes.size(); ++i)
    {
        for (size_t j = i + 1u; j + 1u < planes.size(); ++j)
        {
            for (size_t k = j + 1u; k < planes.size(); ++k)
            {
                bx::Vec3 candidate{0.0f, 0.0f, 0.0f};
                if (!solvePlaneIntersection(planes[i], planes[j], planes[k], candidate))
                {
                    continue;
                }
                if (!isInsideAllPlanes(candidate, planes))
                {
                    continue;
                }

                bool duplicate = false;
                for (const bx::Vec3 &existing : uniqueVertices)
                {
                    if (nearlyEqual(existing, candidate, 5.0f * kEpsilon))
                    {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate)
                {
                    uniqueVertices.push_back(candidate);
                }
            }
        }
    }

    if (uniqueVertices.size() < 4u)
    {
        error = "Voronoi point set does not produce a bounded convex cell";
        return false;
    }

    // Compute convex-cell volume from face polygons. We normalize each generated
    // Voronoi mesh to unit volume so particle size acts as a pure scale factor.
    double cellVolume = 0.0;

    for (const Plane &plane : planes)
    {
        std::vector<size_t> faceVertexIndices;
        for (size_t vertexIndex = 0u; vertexIndex < uniqueVertices.size(); ++vertexIndex)
        {
            const float distance = bx::dot(plane.normal, uniqueVertices[vertexIndex])
                                   - plane.offset;
            if (std::abs(distance) <= 1.0e-4f)
            {
                faceVertexIndices.push_back(vertexIndex);
            }
        }

        if (faceVertexIndices.size() < 3u)
        {
            continue;
        }

        bx::Vec3 centroid{0.0f, 0.0f, 0.0f};
        for (size_t vertexIndex : faceVertexIndices)
        {
            centroid += uniqueVertices[vertexIndex];
        }
        centroid *= 1.0f / static_cast<float>(faceVertexIndices.size());

        const float normalLength = bx::length(plane.normal);
        if (normalLength <= kEpsilon)
        {
            continue;
        }
        const bx::Vec3 faceNormal = plane.normal * (1.0f / normalLength);
        const bx::Vec3 tangentU = pickPerpendicular(faceNormal);
        const bx::Vec3 tangentV = bx::cross(faceNormal, tangentU);

        struct AngularVertex
        {
            size_t vertexIndex = 0u;
            float angle = 0.0f;
        };

        std::vector<AngularVertex> sorted;
        sorted.reserve(faceVertexIndices.size());
        for (size_t vertexIndex : faceVertexIndices)
        {
            const bx::Vec3 relative = uniqueVertices[vertexIndex] - centroid;
            const float x = bx::dot(relative, tangentU);
            const float y = bx::dot(relative, tangentV);
            sorted.push_back({vertexIndex, std::atan2(y, x)});
        }

        std::sort(sorted.begin(), sorted.end(),
                  [](const AngularVertex &a, const AngularVertex &b) {
                      return a.angle < b.angle;
                  });

        // Face area from polygon area vector projected onto the unit face normal.
        bx::Vec3 areaVector{0.0f, 0.0f, 0.0f};
        for (size_t idx = 0u; idx < sorted.size(); ++idx)
        {
            const bx::Vec3 &current = uniqueVertices[sorted[idx].vertexIndex];
            const bx::Vec3 &next = uniqueVertices[sorted[(idx + 1u) % sorted.size()].vertexIndex];
            areaVector += bx::cross(current, next);
        }
        const double faceArea = 0.5 * std::abs(static_cast<double>(bx::dot(faceNormal, areaVector)));
        const double faceDistance = std::abs(static_cast<double>(plane.offset)
                                             / static_cast<double>(normalLength));
        cellVolume += (faceArea * faceDistance) / 3.0;

        const bx::Vec3 first = uniqueVertices[sorted[0].vertexIndex];
        for (size_t idx = 1u; idx + 1u < sorted.size(); ++idx)
        {
            const bx::Vec3 second = uniqueVertices[sorted[idx].vertexIndex];
            const bx::Vec3 third = uniqueVertices[sorted[idx + 1u].vertexIndex];
            appendTriangle(first, second, third, faceNormal, outMesh.vertices, outMesh.indices);
        }
    }

    if (outMesh.vertices.empty() || outMesh.indices.empty())
    {
        error = "Voronoi point set produced an empty mesh";
        return false;
    }

    if (!(cellVolume > 1.0e-12))
    {
        error = "Voronoi point set produced a zero-volume cell";
        return false;
    }

    const float normalizationScale = static_cast<float>(std::cbrt(1.0 / cellVolume));
    for (PosNormalVertex &vertex : outMesh.vertices)
    {
        vertex.x *= normalizationScale;
        vertex.y *= normalizationScale;
        vertex.z *= normalizationScale;
    }

    return true;
}
