#include "Mesh.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{

std::vector<PosNormalVertex> makeSphereVertices(float radius, uint16_t stacks, uint16_t slices)
{
    std::vector<PosNormalVertex> vertices;
    if (stacks < 2u || slices == 0u)
    {
        return vertices;
    }

    vertices.reserve(2u + static_cast<size_t>(stacks - 1u) * (slices + 1u));

    vertices.push_back({0.0f, radius, 0.0f, 0.0f, 1.0f, 0.0f});

    for (uint16_t stack = 1; stack < stacks; ++stack)
    {
        const float phi =
            static_cast<float>(M_PI) * static_cast<float>(stack) / static_cast<float>(stacks);
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);

        for (uint16_t slice = 0; slice <= slices; ++slice)
        {
            const float theta = 2.0f * static_cast<float>(M_PI) * static_cast<float>(slice)
                                / static_cast<float>(slices);
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);

            const float nx = sinPhi * cosTheta;
            const float ny = cosPhi;
            const float nz = sinPhi * sinTheta;

            vertices.push_back({
                radius * nx,
                radius * ny,
                radius * nz,
                nx,
                ny,
                nz,
            });
        }
    }

    vertices.push_back({0.0f, -radius, 0.0f, 0.0f, -1.0f, 0.0f});

    return vertices;
}

std::vector<uint16_t> makeSphereIndices(uint16_t stacks, uint16_t slices)
{
    std::vector<uint16_t> indices;
    if (stacks < 2u || slices == 0u)
    {
        return indices;
    }

    // Sphere and hemisphere winding differ because their parameterizations rotate around
    // different axes. Indices below are ordered for outward-facing CCW front faces.

    indices.reserve(static_cast<size_t>(stacks - 1u) * slices * 6u);

    const uint16_t ringVertexCount = static_cast<uint16_t>(slices + 1u);
    const uint16_t northPoleIndex = 0u;
    const uint16_t firstRingStart = 1u;
    const uint16_t southPoleIndex = static_cast<uint16_t>(firstRingStart
                                                           + (stacks - 1u)
                                                                 * ringVertexCount);

    for (uint16_t slice = 0; slice < slices; ++slice)
    {
        const uint16_t firstRing = static_cast<uint16_t>(firstRingStart + slice);
        indices.push_back(firstRing);
        indices.push_back(northPoleIndex);
        indices.push_back(static_cast<uint16_t>(firstRing + 1u));
    }

    for (uint16_t ring = 0; ring + 1u < stacks - 1u; ++ring)
    {
        const uint16_t first = static_cast<uint16_t>(firstRingStart + ring * ringVertexCount);
        const uint16_t second = static_cast<uint16_t>(first + ringVertexCount);

        for (uint16_t slice = 0; slice < slices; ++slice)
        {
            const uint16_t firstSlice = static_cast<uint16_t>(first + slice);
            const uint16_t secondSlice = static_cast<uint16_t>(second + slice);
            const uint16_t firstNext = static_cast<uint16_t>(firstSlice + 1u);
            const uint16_t secondNext = static_cast<uint16_t>(secondSlice + 1u);

            indices.push_back(firstSlice);
            indices.push_back(firstNext);
            indices.push_back(secondSlice);

            indices.push_back(secondSlice);
            indices.push_back(firstNext);
            indices.push_back(secondNext);
        }
    }

    const uint16_t lastRingStart = static_cast<uint16_t>(southPoleIndex - ringVertexCount);
    for (uint16_t slice = 0; slice < slices; ++slice)
    {
        const uint16_t lastRing = static_cast<uint16_t>(lastRingStart + slice);
        indices.push_back(lastRing);
        indices.push_back(static_cast<uint16_t>(lastRing + 1u));
        indices.push_back(southPoleIndex);
    }

    return indices;
}

std::vector<PosNormalVertex> makeCylinderVertices(float radius, float length,
                                                  uint16_t slices)
{
    std::vector<PosNormalVertex> vertices;
    vertices.reserve((slices + 1u) * 2u);

    const float halfLength = 0.5f * length;
    for (uint16_t slice = 0; slice <= slices; ++slice)
    {
        const float theta = 2.0f * static_cast<float>(M_PI) * static_cast<float>(slice)
                            / static_cast<float>(slices);
        const float cosTheta = std::cos(theta);
        const float sinTheta = std::sin(theta);
        const float x = radius * cosTheta;
        const float y = radius * sinTheta;

        vertices.push_back({x, y, -halfLength, cosTheta, sinTheta, 0.0f});
        vertices.push_back({x, y, halfLength, cosTheta, sinTheta, 0.0f});
    }

    return vertices;
}

std::vector<uint16_t> makeCylinderIndices(uint16_t slices)
{
    std::vector<uint16_t> indices;
    indices.reserve(slices * 6u);

    for (uint16_t slice = 0; slice < slices; ++slice)
    {
        const uint16_t first = static_cast<uint16_t>(slice * 2u);
        const uint16_t second = static_cast<uint16_t>(first + 1u);
        const uint16_t nextFirst = static_cast<uint16_t>(first + 2u);
        const uint16_t nextSecond = static_cast<uint16_t>(first + 3u);

        indices.push_back(first);
        indices.push_back(nextFirst);
        indices.push_back(second);

        indices.push_back(second);
        indices.push_back(nextFirst);
        indices.push_back(nextSecond);
    }

    return indices;
}

std::vector<PosNormalVertex> makeHemisphereVertices(float radius, uint16_t stacks,
                                                    uint16_t slices)
{
    std::vector<PosNormalVertex> vertices;
    if (stacks == 0u || slices == 0u)
    {
        return vertices;
    }

    vertices.reserve(1u + static_cast<size_t>(stacks) * (slices + 1u));
    vertices.push_back({0.0f, 0.0f, radius, 0.0f, 0.0f, 1.0f});

    for (uint16_t stack = 1; stack <= stacks; ++stack)
    {
        const float phi = 0.5f * static_cast<float>(M_PI) * static_cast<float>(stack)
                          / static_cast<float>(stacks);
        const float ringRadius = std::sin(phi);
        const float z = std::cos(phi);

        for (uint16_t slice = 0; slice <= slices; ++slice)
        {
            const float theta = 2.0f * static_cast<float>(M_PI) * static_cast<float>(slice)
                                / static_cast<float>(slices);
            const float cosTheta = std::cos(theta);
            const float sinTheta = std::sin(theta);
            const float nx = ringRadius * cosTheta;
            const float ny = ringRadius * sinTheta;
            const float nz = z;

            vertices.push_back({radius * nx, radius * ny, radius * nz,
                                nx, ny, nz});
        }
    }

    return vertices;
}

std::vector<uint16_t> makeHemisphereIndices(uint16_t stacks, uint16_t slices)
{
    std::vector<uint16_t> indices;
    if (stacks == 0u || slices == 0u)
    {
        return indices;
    }

    // This winding intentionally differs from makeSphereIndices: the hemisphere vertices are
    // parameterized around the z axis rather than the sphere's y axis. Indices below are
    // ordered for outward-facing CCW front faces.

    indices.reserve(static_cast<size_t>(2u * stacks - 1u) * slices * 3u);

    const uint16_t ringVertexCount = static_cast<uint16_t>(slices + 1u);
    const uint16_t northPoleIndex = 0u;
    const uint16_t firstRingStart = 1u;

    for (uint16_t slice = 0; slice < slices; ++slice)
    {
        const uint16_t firstRing = static_cast<uint16_t>(firstRingStart + slice);
        indices.push_back(firstRing);
        indices.push_back(static_cast<uint16_t>(firstRing + 1u));
        indices.push_back(northPoleIndex);
    }

    for (uint16_t ring = 0; ring + 1u < stacks; ++ring)
    {
        const uint16_t first = static_cast<uint16_t>(firstRingStart + ring * ringVertexCount);
        const uint16_t second = static_cast<uint16_t>(first + ringVertexCount);

        for (uint16_t slice = 0; slice < slices; ++slice)
        {
            const uint16_t firstSlice = static_cast<uint16_t>(first + slice);
            const uint16_t secondSlice = static_cast<uint16_t>(second + slice);
            const uint16_t firstNext = static_cast<uint16_t>(firstSlice + 1u);
            const uint16_t secondNext = static_cast<uint16_t>(secondSlice + 1u);

            indices.push_back(firstSlice);
            indices.push_back(secondSlice);
            indices.push_back(firstNext);

            indices.push_back(secondSlice);
            indices.push_back(secondNext);
            indices.push_back(firstNext);
        }
    }

    return indices;
}

std::vector<PosNormalVertex> makeSphericalCapVertices(float radius, float cosHalfAngle,
                                                      uint16_t stacks, uint16_t slices)
{
    std::vector<PosNormalVertex> vertices;
    if (stacks == 0u || slices == 0u)
    {
        return vertices;
    }

    const float clampedCos = std::clamp(cosHalfAngle, -1.0f, 1.0f);
    const float maxPhi = std::acos(clampedCos);

    vertices.reserve(1u + static_cast<size_t>(stacks) * (slices + 1u));
    vertices.push_back({0.0f, 0.0f, radius, 0.0f, 0.0f, 1.0f});

    for (uint16_t stack = 1; stack <= stacks; ++stack)
    {
        const float phi = maxPhi * static_cast<float>(stack) / static_cast<float>(stacks);
        const float ringRadius = std::sin(phi);
        const float z = std::cos(phi);

        for (uint16_t slice = 0; slice <= slices; ++slice)
        {
            const float theta = 2.0f * static_cast<float>(M_PI) * static_cast<float>(slice)
                                / static_cast<float>(slices);
            const float cosTheta = std::cos(theta);
            const float sinTheta = std::sin(theta);
            const float nx = ringRadius * cosTheta;
            const float ny = ringRadius * sinTheta;
            const float nz = z;

            vertices.push_back({radius * nx, radius * ny, radius * nz,
                                nx, ny, nz});
        }
    }

    return vertices;
}

std::vector<uint16_t> makeSphericalCapIndices(uint16_t stacks, uint16_t slices)
{
    return makeHemisphereIndices(stacks, slices);
}

std::vector<PosNormalVertex> makeConeVertices(float radius, float height,
                                              uint16_t slices)
{
    std::vector<PosNormalVertex> vertices;
    vertices.reserve((slices + 1u) * 2u);

    const float slope = height > 0.0f ? radius / height : 0.0f;
    for (uint16_t slice = 0; slice <= slices; ++slice)
    {
        const float theta = 2.0f * static_cast<float>(M_PI) * static_cast<float>(slice)
                            / static_cast<float>(slices);
        const float cosTheta = std::cos(theta);
        const float sinTheta = std::sin(theta);
        const float nx = cosTheta;
        const float ny = sinTheta;
        const float nz = slope;
        const float normalLength = std::sqrt(nx * nx + ny * ny + nz * nz);
        const float normalizedX = nx / normalLength;
        const float normalizedY = ny / normalLength;
        const float normalizedZ = nz / normalLength;

        vertices.push_back({radius * cosTheta, radius * sinTheta, 0.0f,
                            normalizedX, normalizedY, normalizedZ});
        vertices.push_back({0.0f, 0.0f, height,
                            normalizedX, normalizedY, normalizedZ});
    }

    return vertices;
}

std::vector<uint16_t> makeConeIndices(uint16_t slices)
{
    std::vector<uint16_t> indices;
    indices.reserve(slices * 6u);

    for (uint16_t slice = 0; slice < slices; ++slice)
    {
        const uint16_t first = static_cast<uint16_t>(slice * 2u);
        const uint16_t second = static_cast<uint16_t>(first + 1u);
        const uint16_t nextFirst = static_cast<uint16_t>(first + 2u);
        const uint16_t nextSecond = static_cast<uint16_t>(first + 3u);

        indices.push_back(first);
        indices.push_back(nextFirst);
        indices.push_back(second);

        indices.push_back(second);
        indices.push_back(nextFirst);
        indices.push_back(nextSecond);
    }

    return indices;
}

std::vector<PosNormalVertex> makeBoxVertices(float halfExtent)
{
    return {
        // Front (+Z)
        {-halfExtent, -halfExtent, halfExtent, 0.0f, 0.0f, 1.0f},
        {halfExtent, -halfExtent, halfExtent, 0.0f, 0.0f, 1.0f},
        {halfExtent, halfExtent, halfExtent, 0.0f, 0.0f, 1.0f},
        {-halfExtent, halfExtent, halfExtent, 0.0f, 0.0f, 1.0f},

        // Back (-Z)
        {halfExtent, -halfExtent, -halfExtent, 0.0f, 0.0f, -1.0f},
        {-halfExtent, -halfExtent, -halfExtent, 0.0f, 0.0f, -1.0f},
        {-halfExtent, halfExtent, -halfExtent, 0.0f, 0.0f, -1.0f},
        {halfExtent, halfExtent, -halfExtent, 0.0f, 0.0f, -1.0f},

        // Right (+X)
        {halfExtent, -halfExtent, halfExtent, 1.0f, 0.0f, 0.0f},
        {halfExtent, -halfExtent, -halfExtent, 1.0f, 0.0f, 0.0f},
        {halfExtent, halfExtent, -halfExtent, 1.0f, 0.0f, 0.0f},
        {halfExtent, halfExtent, halfExtent, 1.0f, 0.0f, 0.0f},

        // Left (-X)
        {-halfExtent, -halfExtent, -halfExtent, -1.0f, 0.0f, 0.0f},
        {-halfExtent, -halfExtent, halfExtent, -1.0f, 0.0f, 0.0f},
        {-halfExtent, halfExtent, halfExtent, -1.0f, 0.0f, 0.0f},
        {-halfExtent, halfExtent, -halfExtent, -1.0f, 0.0f, 0.0f},

        // Top (+Y)
        {-halfExtent, halfExtent, halfExtent, 0.0f, 1.0f, 0.0f},
        {halfExtent, halfExtent, halfExtent, 0.0f, 1.0f, 0.0f},
        {halfExtent, halfExtent, -halfExtent, 0.0f, 1.0f, 0.0f},
        {-halfExtent, halfExtent, -halfExtent, 0.0f, 1.0f, 0.0f},

        // Bottom (-Y)
        {-halfExtent, -halfExtent, -halfExtent, 0.0f, -1.0f, 0.0f},
        {halfExtent, -halfExtent, -halfExtent, 0.0f, -1.0f, 0.0f},
        {halfExtent, -halfExtent, halfExtent, 0.0f, -1.0f, 0.0f},
        {-halfExtent, -halfExtent, halfExtent, 0.0f, -1.0f, 0.0f},
    };
}

std::vector<uint16_t> makeBoxIndices()
{
    return {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4,
        8, 9, 10, 10, 11, 8,
        12, 13, 14, 14, 15, 12,
        16, 17, 18, 18, 19, 16,
        20, 21, 22, 22, 23, 20,
    };
}

std::vector<PosNormalVertex> makeOctahedronVertices(float radius)
{
    std::vector<PosNormalVertex> vertices;
    vertices.reserve(24u);

    struct Point
    {
        float x;
        float y;
        float z;
    };

    const Point top{0.0f, radius, 0.0f};
    const Point bottom{0.0f, -radius, 0.0f};
    const Point east{radius, 0.0f, 0.0f};
    const Point west{-radius, 0.0f, 0.0f};
    const Point north{0.0f, 0.0f, radius};
    const Point south{0.0f, 0.0f, -radius};

    const auto appendFace = [&](const Point &a, const Point &b, const Point &c) {
        const float abX = b.x - a.x;
        const float abY = b.y - a.y;
        const float abZ = b.z - a.z;
        const float acX = c.x - a.x;
        const float acY = c.y - a.y;
        const float acZ = c.z - a.z;
        float normalX = abY * acZ - abZ * acY;
        float normalY = abZ * acX - abX * acZ;
        float normalZ = abX * acY - abY * acX;
        const float normalLength = std::sqrt(normalX * normalX + normalY * normalY
                                             + normalZ * normalZ);
        if (normalLength > 1.0e-6f)
        {
            normalX /= normalLength;
            normalY /= normalLength;
            normalZ /= normalLength;
        }

        vertices.push_back({a.x, a.y, a.z, normalX, normalY, normalZ});
        vertices.push_back({b.x, b.y, b.z, normalX, normalY, normalZ});
        vertices.push_back({c.x, c.y, c.z, normalX, normalY, normalZ});
    };

    appendFace(top, north, east);
    appendFace(top, east, south);
    appendFace(top, south, west);
    appendFace(top, west, north);
    appendFace(bottom, east, north);
    appendFace(bottom, south, east);
    appendFace(bottom, west, south);
    appendFace(bottom, north, west);

    return vertices;
}

std::vector<uint16_t> makeOctahedronIndices()
{
    std::vector<uint16_t> indices(24u);
    for (uint16_t index = 0; index < indices.size(); ++index)
    {
        indices[index] = index;
    }

    return indices;
}

std::vector<PosNormalVertex> makeRegularPolygonVertices(uint16_t sideCount)
{
    std::vector<PosNormalVertex> vertices;
    if (sideCount < 3u)
    {
        return vertices;
    }

    vertices.reserve(2u + 2u * sideCount);

    vertices.push_back({0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f});
    for (uint16_t sideIndex = 0; sideIndex < sideCount; ++sideIndex)
    {
        const float theta = 2.0f * static_cast<float>(M_PI) * static_cast<float>(sideIndex)
                            / static_cast<float>(sideCount);
        vertices.push_back({std::cos(theta), std::sin(theta), 0.0f,
                            0.0f, 0.0f, 1.0f});
    }

    vertices.push_back({0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f});
    for (uint16_t sideIndex = 0; sideIndex < sideCount; ++sideIndex)
    {
        const float theta = 2.0f * static_cast<float>(M_PI) * static_cast<float>(sideIndex)
                            / static_cast<float>(sideCount);
        vertices.push_back({std::cos(theta), std::sin(theta), 0.0f,
                            0.0f, 0.0f, -1.0f});
    }

    return vertices;
}

std::vector<uint16_t> makeRegularPolygonIndices(uint16_t sideCount)
{
    std::vector<uint16_t> indices;
    if (sideCount < 3u)
    {
        return indices;
    }

    indices.reserve(static_cast<size_t>(sideCount) * 6u);

    const uint16_t frontCenterIndex = 0u;
    for (uint16_t sideIndex = 0u; sideIndex < sideCount; ++sideIndex)
    {
        const uint16_t currentIndex = static_cast<uint16_t>(1u + sideIndex);
        const uint16_t nextIndex = static_cast<uint16_t>(1u + ((sideIndex + 1u) % sideCount));
        indices.push_back(frontCenterIndex);
        indices.push_back(currentIndex);
        indices.push_back(nextIndex);
    }

    const uint16_t backCenterIndex = static_cast<uint16_t>(sideCount + 1u);
    const uint16_t backStartIndex = static_cast<uint16_t>(backCenterIndex + 1u);
    for (uint16_t sideIndex = 0u; sideIndex < sideCount; ++sideIndex)
    {
        const uint16_t currentIndex = static_cast<uint16_t>(backStartIndex + sideIndex);
        const uint16_t nextIndex = static_cast<uint16_t>(backStartIndex
                                                         + ((sideIndex + 1u) % sideCount));
        indices.push_back(backCenterIndex);
        indices.push_back(nextIndex);
        indices.push_back(currentIndex);
    }

    return indices;
}

} // namespace

void PosNormalVertex::init(bgfx::VertexLayout &layout)
{
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .end();
}

Mesh::~Mesh()
{
    reset();
}

Mesh::Mesh(Mesh &&other) noexcept
    : m_vertexBuffer(other.m_vertexBuffer), m_indexBuffer(other.m_indexBuffer)
{
    other.m_vertexBuffer = BGFX_INVALID_HANDLE;
    other.m_indexBuffer = BGFX_INVALID_HANDLE;
}

Mesh &Mesh::operator=(Mesh &&other) noexcept
{
    if (this != &other)
    {
        reset();
        m_vertexBuffer = other.m_vertexBuffer;
        m_indexBuffer = other.m_indexBuffer;
        other.m_vertexBuffer = BGFX_INVALID_HANDLE;
        other.m_indexBuffer = BGFX_INVALID_HANDLE;
    }
    return *this;
}

Mesh Mesh::createSphere(float radius, uint16_t stacks, uint16_t slices,
                        const bgfx::VertexLayout &layout)
{
    Mesh mesh;
    mesh.upload(makeSphereVertices(radius, stacks, slices), makeSphereIndices(stacks, slices),
                layout);
    return mesh;
}

Mesh Mesh::createCylinder(float radius, float length, uint16_t slices,
                          const bgfx::VertexLayout &layout)
{
    Mesh mesh;
    mesh.upload(makeCylinderVertices(radius, length, slices),
                makeCylinderIndices(slices), layout);
    return mesh;
}

Mesh Mesh::createHemisphere(float radius, uint16_t stacks, uint16_t slices,
                            const bgfx::VertexLayout &layout)
{
    Mesh mesh;
    mesh.upload(makeHemisphereVertices(radius, stacks, slices),
                makeHemisphereIndices(stacks, slices), layout);
    return mesh;
}

Mesh Mesh::createSphericalCap(float radius, float cosHalfAngle, uint16_t stacks,
                              uint16_t slices, const bgfx::VertexLayout &layout)
{
    Mesh mesh;
    mesh.upload(makeSphericalCapVertices(radius, cosHalfAngle, stacks, slices),
                makeSphericalCapIndices(stacks, slices), layout);
    return mesh;
}

Mesh Mesh::createCone(float radius, float height, uint16_t slices,
                      const bgfx::VertexLayout &layout)
{
    Mesh mesh;
    mesh.upload(makeConeVertices(radius, height, slices),
                makeConeIndices(slices), layout);
    return mesh;
}

Mesh Mesh::createBox(float halfExtent, const bgfx::VertexLayout &layout)
{
    Mesh mesh;
    mesh.upload(makeBoxVertices(halfExtent), makeBoxIndices(), layout);
    return mesh;
}

Mesh Mesh::createOctahedron(float radius, const bgfx::VertexLayout &layout)
{
    Mesh mesh;
    mesh.upload(makeOctahedronVertices(radius), makeOctahedronIndices(), layout);
    return mesh;
}

Mesh Mesh::createRegularPolygon(uint16_t sideCount, const bgfx::VertexLayout &layout)
{
    Mesh mesh;
    mesh.upload(makeRegularPolygonVertices(sideCount), makeRegularPolygonIndices(sideCount),
                layout);
    return mesh;
}

bool Mesh::upload(const std::vector<PosNormalVertex> &vertices,
                  const std::vector<uint16_t> &indices, const bgfx::VertexLayout &layout)
{
    reset();

    if (vertices.empty() || indices.empty())
    {
        return false;
    }

    const bgfx::Memory *vertexMemory = bgfx::copy(
        vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(PosNormalVertex)));
    const bgfx::Memory *indexMemory =
        bgfx::copy(indices.data(), static_cast<uint32_t>(indices.size() * sizeof(uint16_t)));

    m_vertexBuffer = bgfx::createVertexBuffer(vertexMemory, layout);
    m_indexBuffer = bgfx::createIndexBuffer(indexMemory);
    return isValid();
}

bool Mesh::isValid() const
{
    return bgfx::isValid(m_vertexBuffer) && bgfx::isValid(m_indexBuffer);
}

void Mesh::bind() const
{
    bgfx::setVertexBuffer(0, m_vertexBuffer);
    bgfx::setIndexBuffer(m_indexBuffer);
}

void Mesh::reset()
{
    if (bgfx::isValid(m_vertexBuffer))
    {
        bgfx::destroy(m_vertexBuffer);
        m_vertexBuffer = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_indexBuffer))
    {
        bgfx::destroy(m_indexBuffer);
        m_indexBuffer = BGFX_INVALID_HANDLE;
    }
}