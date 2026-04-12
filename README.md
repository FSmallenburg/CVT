# Colloid Visualization Tool (CVT)

CVT is a bgfx/GLFW-based viewer for colloid and particle trajectory files.

## Quick start

For a typical fresh setup:

```bash
git clone --recurse-submodules <repo-url>
cd CVT
```

1. **Compile the runtime shaders**:

```bash
./compileshaders.sh
```

Then build CVT using the preset for your platform:

- **Linux:** `cmake --preset debug` then `cmake --build --preset debug`
- **Windows (MSYS2 MinGW64):** `cmake --preset mingw-debug` then `cmake --build --preset build-mingw-debug`
- **macOS:** `cmake --preset macos-debug` then `cmake --build --preset build-macos-debug`



## Build overview

**bgfx**, **bx**, and **bimg** are bundled as nested submodules inside `third_party/bgfx.cmake`
and are compiled automatically by `cmake --build`. No separate pre-build step is needed.

For a fresh checkout, prefer:

```bash
git clone --recurse-submodules <repo-url>
```

If you already have the repo, pull and refresh submodules with:

```bash
git pull --recurse-submodules
git submodule update --init --recursive
```

Compile the runtime shaders before the first run:

```bash
./compileshaders.sh
```

> **Note:** `compileshaders.sh` needs a `shaderc` binary to compile the shaders.
> It searches several known locations automatically (including the project build
> directories and `third_party/bgfx.cmake/bgfx/tools/bin/<platform>/shaderc`).
> If `shaderc` is not found, the script will build it automatically from
> `third_party/bgfx.cmake` into `build-shaderc/` using CMake — no manual bgfx
> tool build is required. You can override the binary path with the `SHADERC`
> environment variable.

---

## Linux build notes

### Prerequisites

Install a C++ toolchain plus the GLFW and pkg-config development packages.
For example on Debian/Ubuntu:

```bash
sudo apt install build-essential cmake ninja-build pkg-config libglfw3-dev
```

### Configure and build

Linux presets are already included in `CMakePresets.json`:

```bash
cmake --preset debug
cmake --build --preset debug

cmake --preset release
cmake --build --preset release
```

You can also configure manually:

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

### Run

```bash
./build-release/cvt TestInputFiles/polydisperse.osph
```

### Notes

- The current Linux path uses the **OpenGL** renderer.
- If CMake reports `GLFW 3 not found`, make sure `glfw3.pc` is visible to `pkg-config`.

---

## Windows build notes

### Recommended environment

The current project presets are set up for **MSYS2 MinGW64**.
Install at least from an **MSYS2 MinGW64 shell**:

```bash
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja mingw-w64-x86_64-pkgconf mingw-w64-x86_64-glfw
```

### Compile shaders

From PowerShell, the known working invocation is:

```powershell
C:\msys64\usr\bin\sh.exe ./compileshaders.sh
```

### Configure and build

Use the included presets:

```powershell
cmake --preset mingw-debug
cmake --build --preset build-mingw-debug

cmake --preset mingw-release
cmake --build --preset build-mingw-release
```

### Run

```powershell
.\build-release\cvt.exe TestInputFiles\polydisperse.osph
```

### Notes

- The current Windows preference order is **OpenGL**, then **Direct3D11**.
- bgfx is compiled automatically as part of the CMake build; no separate bgfx build step is needed.

---

## macOS build notes

### Prerequisites

Install Xcode Command Line Tools and Homebrew packages:

```bash
xcode-select --install
brew install cmake ninja pkg-config glfw
```

### Compile shaders

On macOS, `compileshaders.sh` builds both **GLSL** and **Metal** shader outputs by default:

```bash
./compileshaders.sh
```

### Configure and build

A dedicated macOS preset is included:

```bash
cmake --preset macos-debug
cmake --build --preset build-macos-debug

cmake --preset macos-release
cmake --build --preset build-macos-release
```

You can still configure manually if needed:

```bash
cmake -S . -B build-mac-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build build-mac-release
```

On Apple Silicon, set the architecture explicitly if needed:

```bash
cmake -S . -B build-mac-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix)" \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-mac-release
```

### Run

```bash
./build-mac-release/cvt TestInputFiles/polydisperse.osph
```

### Notes

- The current macOS preference order is **Metal**, then **OpenGL**.
- If you package the app as a `.app`, CVT will also search under `Contents/Resources` for runtime assets.
- Be careful not to mix `arm64` and `x86_64` bgfx builds.

---

## Common issues

### Shaders fail to load at runtime

Re-run:

```bash
./compileshaders.sh
```

and confirm that the expected directories exist under `shaders/`:

- `glsl/`
- `dxbc/` (Windows)
- `metal/` (macOS)

### `GLFW 3 not found`

Install GLFW development files and make sure CMake can find either:

- a `glfw` / `glfw3` CMake target, or
- `glfw3` through `pkg-config`
