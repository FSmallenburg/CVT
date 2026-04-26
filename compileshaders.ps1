#Requires -Version 5.1
<#
.SYNOPSIS
    Compile bgfx runtime shaders for Windows (GLSL + DXBC) using shaderc.exe.
.DESCRIPTION
    Mirrors compileshaders.sh for use in environments where a POSIX shell is
    unavailable (e.g. a pure MSVC build).  Finds or builds shaderc.exe, then
    calls it for each shader source file.

    Environment variable overrides (all optional):
        SHADERC                     - Path to shaderc.exe to use directly.
        BGFX_BUILD_DIR              - bgfx build output directory to search for shaderc.
        BGFX_SHADERC_BUILD_DIR      - Dedicated shaderc build directory (default: build-shaderc-msvc).
        BGFX_SHADERC_AUTOBUILD      - Set to "0" to disable auto-build of shaderc (default: 1).
        BGFX_SHADER_OPTIMIZATION_LEVEL - shaderc -O level (default: 3).
        BGFX_D3D_SHADER_PROFILE     - DXBC profile (default: s_5_0).
        BGFX_SHADER_PROFILE         - GLSL profile for most shaders (default: 120).
        BGFX_STRUCTURE_FACTOR_SHADER_PROFILE - GLSL profile for structure-factor shaders (default: 150).
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = $PSScriptRoot

$bgfxDir              = Join-Path $scriptDir "third_party\bgfx.cmake\bgfx"
$bgfxIncludeDir       = Join-Path $bgfxDir "src"
$shadercBuildDir      = if ($env:BGFX_SHADERC_BUILD_DIR) { $env:BGFX_SHADERC_BUILD_DIR } else { Join-Path $scriptDir "build-shaderc-msvc" }
$autoBuild            = if ($env:BGFX_SHADERC_AUTOBUILD -eq "0") { $false } else { $true }
$optimizationLevel    = if ($env:BGFX_SHADER_OPTIMIZATION_LEVEL) { $env:BGFX_SHADER_OPTIMIZATION_LEVEL } else { "3" }
$d3dProfile           = if ($env:BGFX_D3D_SHADER_PROFILE) { $env:BGFX_D3D_SHADER_PROFILE } else { "s_5_0" }
$glslProfile          = if ($env:BGFX_SHADER_PROFILE) { $env:BGFX_SHADER_PROFILE } else { "120" }
$structureGlslProfile = if ($env:BGFX_STRUCTURE_FACTOR_SHADER_PROFILE) { $env:BGFX_STRUCTURE_FACTOR_SHADER_PROFILE } else { "150" }

function Find-Shaderc {
    $candidates = [System.Collections.Generic.List[string]]::new()

    if ($env:SHADERC) { $candidates.Add($env:SHADERC) }

    if ($env:BGFX_BUILD_DIR) {
        foreach ($name in @("shadercRelease.exe","shadercDebug.exe","shaderc.exe")) {
            $candidates.Add((Join-Path $env:BGFX_BUILD_DIR "bin\$name"))
            $candidates.Add((Join-Path $env:BGFX_BUILD_DIR $name))
        }
    }

    # Dedicated shaderc build dir
    foreach ($name in @("shadercRelease.exe","shadercDebug.exe","shaderc.exe")) {
        $candidates.Add((Join-Path $shadercBuildDir "Release\$name"))
        $candidates.Add((Join-Path $shadercBuildDir "bin\Release\$name"))
        $candidates.Add((Join-Path $shadercBuildDir $name))
    }

    # Pre-built bgfx tools bundled in the repo
    $candidates.Add((Join-Path $bgfxDir "tools\bin\windows\shaderc.exe"))

    # MinGW build outputs
    foreach ($buildSubdir in @("build-release","build-debug")) {
        foreach ($name in @("shaderc.exe","shadercRelease.exe","shadercDebug.exe")) {
            $candidates.Add((Join-Path $scriptDir "$buildSubdir\$name"))
        }
    }

    foreach ($c in $candidates) {
        if (Test-Path $c -PathType Leaf) { return $c }
    }

    # Recursive search in shaderc build dir as a last resort
    if (Test-Path $shadercBuildDir -PathType Container) {
        $found = Get-ChildItem -Path $shadercBuildDir -Recurse -Include "shadercRelease.exe","shadercDebug.exe","shaderc.exe" -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
        if ($found) { return $found }
    }

    return $null
}

function Build-Shaderc {
    # Build shaderc by configuring third_party/bgfx.cmake directly — not through the CVT
    # project, which forces BGFX_BUILD_TOOLS=OFF.  This mirrors build_shaderc() in compileshaders.sh.
    $bgfxCmakeSource = Join-Path $scriptDir "third_party\bgfx.cmake"
    if (-not (Test-Path $bgfxCmakeSource)) {
        throw "bgfx.cmake source not found at $bgfxCmakeSource"
    }

    # Wipe a stale cache if the recorded source directory doesn't match.
    $cacheFile = Join-Path $shadercBuildDir "CMakeCache.txt"
    if (Test-Path $cacheFile) {
        $cachedHome = Get-Content $cacheFile | Select-String "^CMAKE_HOME_DIRECTORY:INTERNAL=" |
            Select-Object -First 1 | ForEach-Object { $_ -replace "^CMAKE_HOME_DIRECTORY:INTERNAL=", "" }
        $normalSource = $bgfxCmakeSource -replace "\\", "/"
        $normalCached = "$cachedHome" -replace "\\", "/"
        if ($normalCached -and $normalCached -ne $normalSource) {
            Write-Host "Stale build-shaderc-msvc cache from '$normalCached'; wiping for '$normalSource'" -ForegroundColor Yellow
            Remove-Item -Recurse -Force $shadercBuildDir
        }
    }

    $configureArgs = @(
        "-S", $bgfxCmakeSource,
        "-B", $shadercBuildDir,
        "-G", "Visual Studio 18 2026",
        "-A", "x64",
        "-DBGFX_BUILD_EXAMPLES=OFF",
        "-DBGFX_BUILD_TOOLS=ON",
        "-DBGFX_BUILD_TOOLS_SHADER=ON",
        "-DBGFX_BUILD_TOOLS_TEXTURE=OFF",
        "-DBGFX_BUILD_TOOLS_GEOMETRY=OFF",
        "-DBGFX_BUILD_TOOLS_BIN2C=OFF",
        "-DBGFX_BUILD_TESTS=OFF",
        "-DBGFX_INSTALL=OFF",
        "-DBGFX_CUSTOM_TARGETS=OFF"
    )

    & cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { throw "cmake configure for shaderc failed (exit $LASTEXITCODE)" }

    & cmake --build $shadercBuildDir --target shaderc --config Release
    if ($LASTEXITCODE -ne 0) { throw "cmake build for shaderc failed (exit $LASTEXITCODE)" }

    $result = Find-Shaderc
    if (-not $result) { throw "shaderc.exe still not found after auto-build." }
    return $result
}

$shaderc = Find-Shaderc
if (-not $shaderc) {
    if ($autoBuild) {
        $shaderc = Build-Shaderc
    } else {
        throw "shaderc.exe not found. Set SHADERC, BGFX_BUILD_DIR, or set BGFX_SHADERC_AUTOBUILD=1."
    }
}
Write-Host "Using shaderc: $shaderc"

if (-not (Test-Path $bgfxIncludeDir -PathType Container)) {
    throw "bgfx shader include directory not found at $bgfxIncludeDir"
}

$shadersDir  = Join-Path $scriptDir "shaders"
$varyingDef  = Join-Path $shadersDir "varying.def.sc"
$commonArgs  = @("--varyingdef", $varyingDef, "-i", $shadersDir, "-i", $bgfxIncludeDir)

function Compile-Shader {
    param(
        [string]$InputFile,
        [string]$OutputFile,
        [string]$Type,        # vertex | fragment
        [string]$Backend,     # glsl | dxbc
        [string]$ProfileKind  # regular | structure
    )

    $platform = if ($Backend -eq "dxbc") { "windows" } else { "windows" }

    $profile = switch ($Backend) {
        "dxbc" { $d3dProfile }
        "glsl"  { if ($ProfileKind -eq "structure") { $structureGlslProfile } else { $glslProfile } }
        default { throw "Unknown backend: $Backend" }
    }

    $outDir = Join-Path $shadersDir $Backend
    if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

    $inputPath  = Join-Path $shadersDir $InputFile
    $outputPath = Join-Path $outDir $OutputFile

    $cmd = @($shaderc, "-f", $inputPath, "-o", $outputPath,
             "--type", $Type, "--platform", $platform,
             "-p", $profile, "-O", $optimizationLevel) + $commonArgs

    Write-Host ("+ " + ($cmd -join " "))
    & $cmd[0] $cmd[1..($cmd.Length-1)]
    if ($LASTEXITCODE -ne 0) { throw "shaderc failed for $InputFile -> $OutputFile (exit $LASTEXITCODE)" }
}

$backends = @("glsl", "dxbc")

Write-Host "Compiling shaders for backends: $($backends -join ', ')"

foreach ($backend in $backends) {
    Compile-Shader "vs_instancing.sc"           "vs_instancing.bin"           vertex   $backend regular
    Compile-Shader "fs_instancing.sc"           "fs_instancing.bin"           fragment $backend regular
    Compile-Shader "vs_picking.sc"              "vs_picking.bin"              vertex   $backend regular
    Compile-Shader "fs_picking.sc"              "fs_picking.bin"              fragment $backend regular
    Compile-Shader "vs_lines.sc"                "vs_lines.bin"                vertex   $backend regular
    Compile-Shader "fs_lines.sc"                "fs_lines.bin"                fragment $backend regular
    Compile-Shader "vs_structure_factor.sc"     "vs_structure_factor.bin"     vertex   $backend structure
    Compile-Shader "fs_structure_factor.sc"     "fs_structure_factor.bin"     fragment $backend structure
    Compile-Shader "fs_structure_factor_color.sc" "fs_structure_factor_color.bin" fragment $backend structure
}

Write-Host "Shader compilation complete."
