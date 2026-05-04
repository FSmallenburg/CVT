# Colloid Visualization Tool (CVT)

CVT is a bgfx/GLFW-based viewer for colloid and particle trajectory files. This is still somewhat experimental, but feel free to try it out. A set of sample configuration files are given in the TestInputFiles folder.

DISCLAIMER: Please not that any analysis provided by this tool is intended for quick exploration, and to more easily understand any structure you see in the visualized snapshots. Analysis outputs are not intended for direct publication. If you see something interesting, please run your own version of the analysis to make sure results are fully correct. 

For more information, and a link to directly install this for Windows from the Microsoft store, see the [website](http://www.softmatterdemos.org/CVT).


## Quick start

For a typical fresh setup:

```bash
git clone --recurse-submodules https://github.com/FSmallenburg/CVT
cd CVT
```

Then build CVT using the preset for your platform:

- **Linux:** `cmake --preset release` then `cmake --build --preset release`
- **Windows (MSYS2 MinGW64):** `cmake --preset mingw-release` then `cmake --build --preset build-mingw-release`
- **macOS:** `cmake --preset macos-release` then `cmake --build --preset build-macos-release`

Note that the first build may take some time.

You can use `compileshaders.sh` to recompile the shaders, but in principle this should happen during the cmake build.

## Documentation

- [Command-line usage](docs/command-line-usage.md)
- [GUI usage and file dropping](docs/gui-usage.md)
- [Supported file formats and syntax](docs/file-formats.md)



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

Compile the runtime shaders manually before the first run only if you are not using the CMake build targets:

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


## Windows build notes

### Recommended environment

The current project presets are set up for **MSYS2 MinGW64**.
Install at least from an **MSYS2 MinGW64 shell**:

```bash
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja mingw-w64-x86_64-pkgconf mingw-w64-x86_64-glfw
```

### Configure and build

Use the included presets:

```powershell
cmake --preset mingw-release
cmake --build --preset build-mingw-release
```

### Run

```powershell
.\build-release\cvt.exe TestInputFiles\polydisperse.osph
```

---

## macOS build notes

### Prerequisites

Install Xcode Command Line Tools and Homebrew packages:

```bash
xcode-select --install
brew install cmake ninja pkg-config glfw
```

### Configure and build

A dedicated macOS preset is included:

```bash
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

---

## Common issues

### Shaders fail to load at runtime

Try recompiling the shaders:

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

© 2026 Frank Smallenburg. Released under the [MIT License](LICENSE).
