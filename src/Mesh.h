#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

struct PosNormalVertex
{
    float x, y, z;
    float nx, ny, nz;

    static void init(bgfx::VertexLayout &layout);
};

class Mesh
{
  public:
    Mesh() = default;
    ~Mesh();

    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;

    Mesh(Mesh &&other) noexcept;
    Mesh &operator=(Mesh &&other) noexcept;

    static Mesh createSphere(float radius, uint16_t stacks, uint16_t slices,
                             const bgfx::VertexLayout &layout);
    static Mesh createCylinder(float radius, float length, uint16_t slices,
                               const bgfx::VertexLayout &layout);
    static Mesh createHemisphere(float radius, uint16_t stacks, uint16_t slices,
                                 const bgfx::VertexLayout &layout);
    static Mesh createSphericalCap(float radius, float cosHalfAngle, uint16_t stacks,
                                   uint16_t slices, const bgfx::VertexLayout &layout);
    static Mesh createCone(float radius, float height, uint16_t slices,
                           const bgfx::VertexLayout &layout);
    static Mesh createBox(float halfExtent, const bgfx::VertexLayout &layout);
    static Mesh createOctahedron(float radius, const bgfx::VertexLayout &layout);
    static Mesh createRegularPolygon(uint16_t sideCount, const bgfx::VertexLayout &layout);

    bool upload(const std::vector<PosNormalVertex> &vertices, const std::vector<uint16_t> &indices,
                const bgfx::VertexLayout &layout);

    bool isValid() const;
    void bind() const;

  private:
    void reset();

    bgfx::VertexBufferHandle m_vertexBuffer = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_indexBuffer = BGFX_INVALID_HANDLE;
};