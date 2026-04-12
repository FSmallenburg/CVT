# Supported file formats and syntax

This page documents the trajectory formats currently accepted by CVT.

## Maintenance rule

If a pull request changes extension detection or parsing behavior in `src/TrajectoryReader.cpp`, this document should be updated in the same PR.

## Supported extensions

- .sph: Sphere
- .osph: Ordered sphere
- .dsk: Disk
- .rod: Rod
- .cub: Cube
- .gon: Polygon
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

### .osph

```text
<label> <x> <y> <z> <radius> <order1> [order2 ...]
```

Rules:

- Order parameters are optional; lines with only radius are valid.
- If order parameters are present, all particles in the same frame must provide the same number of order parameters.

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

## Validation checklist

- For each extension, keep one minimal valid example in TestInputFiles.
- Include at least one multi-frame example.
- Include one negative test file with a known parse error (kept outside normal demos).
- Re-run the examples after parser changes.
