#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

/// Interleaved vertex format used by all particle meshes: 3-component
/// position followed by 3-component surface normal.
struct PosNormalVertex
{
    float x, y, z;
    float nx, ny, nz;

    /// Registers this layout with bgfx so vertex buffers can be created.
    static void init(bgfx::VertexLayout &layout);
};

/// Owns a bgfx vertex buffer and index buffer pair that describes a single
/// geometric shape. Meshes are non-copyable; use move semantics to transfer
/// ownership. Instances are created via the static factory methods.
class Mesh
{
  public:
    Mesh() = default;
    ~Mesh();

    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;

    Mesh(Mesh &&other) noexcept;
    Mesh &operator=(Mesh &&other) noexcept;

    /// Creates a UV sphere with the given @p radius, @p stacks latitude bands,
    /// and @p slices longitude bands.
    static Mesh createSphere(float radius, uint16_t stacks, uint16_t slices,
                             const bgfx::VertexLayout &layout);
    /// Creates an open cylinder (no end caps) with @p radius and @p length.
    static Mesh createCylinder(float radius, float length, uint16_t slices,
                               const bgfx::VertexLayout &layout);
    /// Creates the upper hemisphere of a sphere (z >= 0).
    static Mesh createHemisphere(float radius, uint16_t stacks, uint16_t slices,
                                 const bgfx::VertexLayout &layout);
    /// Creates a spherical cap defined by @p cosHalfAngle (cos of the half-opening angle).
    static Mesh createSphericalCap(float radius, float cosHalfAngle, uint16_t stacks,
                                   uint16_t slices, const bgfx::VertexLayout &layout);
    /// Creates a cone with the apex at the origin pointing in +Z, base at z = @p height.
    static Mesh createCone(float radius, float height, uint16_t slices,
                           const bgfx::VertexLayout &layout);
    /// Creates an axis-aligned cube centered at the origin with half-extent @p halfExtent.
    static Mesh createBox(float halfExtent, const bgfx::VertexLayout &layout);
    /// Creates a regular octahedron inscribed in a sphere of @p radius.
    static Mesh createOctahedron(float radius, const bgfx::VertexLayout &layout);
    /// Creates a flat regular polygon with @p sideCount sides lying in the XY plane.
    static Mesh createRegularPolygon(uint16_t sideCount, const bgfx::VertexLayout &layout);

    /// Uploads @p vertices and @p indices to the GPU and stores the resulting
    /// bgfx handles. Returns false if the upload fails.
    bool upload(const std::vector<PosNormalVertex> &vertices, const std::vector<uint16_t> &indices,
                const bgfx::VertexLayout &layout);

    /// Returns true when both handles are valid (i.e. upload() succeeded).
    bool isValid() const;
    /// Sets this mesh's vertex and index buffers as the active bgfx buffers.
    void bind() const;

  private:
    void reset();

    bgfx::VertexBufferHandle m_vertexBuffer = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_indexBuffer = BGFX_INVALID_HANDLE;
};