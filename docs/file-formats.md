# Supported file formats and syntax

This page documents the trajectory formats currently accepted by CVT.

## Maintenance rule

If a pull request changes extension detection or parsing behavior in `src/TrajectoryReader.cpp`, this document should be updated in the same PR.

## Supported extensions

- .sph: Sphere
- .bsph: Bonded sphere
- .osph: Ordered sphere
- .lammpstrj: LAMMPS trajectory
- .dsk: Disk
- .rod: Rod
- .cub: Cube
- .gon: Polygon
- .voro: Convex Voronoi polyhedron
- .ptc: Patchy
- .pat: Patchy legacy
- .patch: Patchy 2D

## Common frame structure

Each frame is parsed as:

1. Frame header line
2. Box line
3. N particle lines (N from frame header)

Notes:

- Blank lines and lines starting with # are ignored.
- Frame header accepts either `N` or `& N`.
- Box line accepts either:
  - rectangular box as three floats: `Lx Ly Lz` (even for 2D systems!)
  - spherical bounds starting with `ball`:
    - `ball originalRadius`
    - `ball originalRadius currentRadius`

LAMMPS `.lammpstrj` files are an exception: they use the native LAMMPS `ITEM:`
section layout instead of the compact CVT frame header + box line format.

## Per-format particle syntax

All particle lines begin with:

```text
<label> <x> <y> <z> ...
```

Note that a z-coordinate is expected to be present even for 2D files!

### .sph and .dsk

```text
<label> <x> <y> <z> <radius>
```

### .bsph

```text
<label> <x> <y> <z> <radius> [bondId ...]
```

Rules:

- Identical to `.sph` for geometry and rendering as spheres.
- Optional trailing bond ids are integers and are treated like patchy bond connectivity.
- When bond mode is enabled, these bonds are rendered the same way as patchy bonds.

### .osph

```text
<label> <x> <y> <z> <radius> <order1> [order2 ...]
```

Rules:

- Order parameters are optional; lines with only radius are valid.
- If order parameters are present, all particles in the same frame must provide the same number of order parameters.

### .lammpstrj

Supported LAMMPS frame structure:

```text
ITEM: TIMESTEP
<step>
ITEM: NUMBER OF ATOMS
<count>
ITEM: BOX BOUNDS [boundary flags]
<xlo> <xhi>
<ylo> <yhi>
<zlo> <zhi>
ITEM: ATOMS <columns...>
<atom row 1>
...
<atom row N>
```

Accepted `ITEM: ATOMS` position columns:

- `x y z`
- `xu yu zu`
- `xs ys zs`
- `xsu ysu zsu`

Required columns:

- `id`
- `type`
- one accepted position triplet from the list above

Optional size columns:

- `radius`
- `diameter` (converted internally to radius)

Rules and current limitations:

- LAMMPS atom ids must be non-negative and unique within each frame.
- Numeric atom types are mapped onto CVT's existing A-Z palette slots, wrapping after 26 types.
- If neither `radius` nor `diameter` is present, CVT uses a default particle radius of `0.5`.
- Orthogonal boxes with arbitrary lower/upper bounds are supported.
- Triclinic dumps with `xy/xz/yz` tilt factors are supported for file loading, wrapping, and minimum-image distance calculations.
- Rectangular-only downstream features such as structure-factor rendering still reject triclinic boxes.
- The current parser expects three box-bound lines and three position coordinates per atom row.
- 2D LAMMPS data are only supported when still emitted in a 3-axis dump layout.

### .rod

```text
<label> <x> <y> <z> <dx> <dy> <dz>
```

or

```text
<label> <x> <y> <z> <diameter> <dx> <dy> <dz>
```

Rules:

- Exactly 3 or 4 trailing numeric fields are required after the particle coordinates.

### .cub

```text
<label> <x> <y> <z> <edgeLength> <r00> <r01> <r02> <r10> <r11> <r12> <r20> <r21> <r22>
```

Rules:

- Requires edge length plus 9 rotation-matrix values.

### .gon

```text
<label> <x> <y> <z> <radius> <sideCount> <angle>
```

Rules:

- This will show as regular polygons in 2D. One vertex is always along the positive x-axis.
- sideCount must be between 3 and 65535.

### .voro

```text
<label> <x> <y> <z> <size> <r00> <r01> <r02> <r10> <r11> <r12> <r20> <r21> <r22>
```

Rules:

- Requires size plus 9 rotation-matrix values.
- The base convex polyhedron is normalized to unit volume before this size factor is applied.
- A sibling file named `voropoints.dat` must exist in the same folder as the `.voro` file.
- `voropoints.dat` contains one or more point-set blocks:
  - One line with integer `k`
  - Followed by `k` lines with `x y z`
- Each block defines one convex particle shape: the Voronoi cell of the origin with respect to that block's points.
- Shape mapping is by particle label (case-insensitive): A/a->shape 0, B/b->shape 1, C/c->shape 2, etc., wrapping when labels exceed available shape blocks.
- If `voropoints.dat` is missing or malformed, file open fails with an error.

### .ptc (patchy)

```text
<label> <x> <y> <z> <coreRadius> <cosHalfAngle> <capDiameter> <r00> ... <r22> [bondId ...]
```

Rules:

- Requires coreRadius, cosHalfAngle, capDiameter, and 9 rotation-matrix values.
- Optional trailing bond ids are integers. The number of these integers indicates the number of patches on the particle
- For the integers, a value of -1 indicates no bond, otherwise the number indicates a bond to a different particle.
- Patches are placed evenly around the particle. Not all patch numbers are supported.
- Supported patch numbers and their position in the reference orientation (see src/PatchPlacement.cpp for exact placement):
    - 1: positive z-direction
    - 2: postive and negative z-direction
    - 3: evenly spaced around the equator
    - 4: tetrahedral
    - 5: triangular bipyramid
    - 6: octahedral
    - 12: icosahedral



### .pat (legacy patchy)

```text
<label> <x> <y> <z> <cosHalfAngle> <r00> ... <r22> [bondId ...]
```

Rules:

- Uses built-in legacy defaults for core/cap size.
- Optional trailing bond ids are integers. (Same as .ptc format)

### .patch (patchy 2D)

Same token layout as .ptc, but interpreted as planar patch placement. Patches are always evenly spaced around the particle circumference.

## Error reporting behavior

Parser errors include frame index and particle index when available.

Examples:

- invalid frame header
- missing particle line
- unsupported extension
- invalid numeric token
- missing or malformed `voropoints.dat` for `.voro`

## Validation checklist

- For each extension, keep one minimal valid example in TestInputFiles.
- Include at least one multi-frame example.
- Include one negative test file with a known parse error (kept outside normal demos).
- Re-run the examples after parser changes.
- For `.lammpstrj`, keep at least one orthogonal multi-frame example and one unsupported triclinic example.
