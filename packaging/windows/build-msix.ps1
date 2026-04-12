param(
    [string]$ConfigurePreset = "mingw-release",
    [string]$BuildPreset = "build-mingw-release",
    [string]$BuildDir = "build-release",
    [string]$StageDir = "out/store/stage",
    [string]$OutputPackage = "out/store/CVT.msix",
    [string]$IdentityName,
    [string]$Publisher,
    [string]$Version = "1.0.0.0",
    [string]$DisplayName = "CVT",
    [string]$PublisherDisplayName = "CVT",
    [string]$Description = "Colloid Visualization Tool",
    [ValidateSet("x64", "x86", "arm64")]
    [string]$Architecture = "x64",
    [string]$PfxPath,
    [string]$PfxPassword,
    [string]$CertThumbprint,
    [switch]$SkipConfigureBuild,
    [switch]$SkipSign
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Require-Tool([string]$Name) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $cmd) {
        throw "Required tool '$Name' not found in PATH."
    }
    return $cmd.Source
}

function Replace-Token([string]$Text, [string]$Token, [string]$Value) {
    return $Text.Replace($Token, $Value)
}

if ([string]::IsNullOrWhiteSpace($IdentityName)) {
    throw "IdentityName is required. Example: --IdentityName com.yourcompany.cvt"
}
if ([string]::IsNullOrWhiteSpace($Publisher)) {
    throw "Publisher is required. Example: --Publisher \"CN=Your Publisher\""
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$stageAbs = Join-Path $repoRoot $StageDir
$outputAbs = Join-Path $repoRoot $OutputPackage
$templatePath = Join-Path $PSScriptRoot "AppxManifest.template.xml"
$assetsPath = Join-Path $PSScriptRoot "Assets"

$requiredAssets = @(
    "StoreLogo.png",
    "Square44x44Logo.png",
    "Square150x150Logo.png",
    "Wide310x150Logo.png",
    "SplashScreen.png"
)

foreach ($asset in $requiredAssets) {
    if (-not (Test-Path (Join-Path $assetsPath $asset))) {
        throw "Missing asset '$asset' in packaging/windows/Assets."
    }
}

if ($SkipConfigureBuild) {
    Write-Host "Skipping configure/build as requested."
} else {
    & cmake --preset $ConfigurePreset
    & cmake --build --preset $BuildPreset
}

if (Test-Path $stageAbs) {
    Remove-Item -Path $stageAbs -Recurse -Force
}
New-Item -ItemType Directory -Path $stageAbs | Out-Null

& cmake --install $BuildDir --prefix $stageAbs

New-Item -ItemType Directory -Path (Join-Path $stageAbs "Assets") -Force | Out-Null
Copy-Item -Path (Join-Path $assetsPath "*.png") -Destination (Join-Path $stageAbs "Assets") -Force

$manifest = Get-Content -Path $templatePath -Raw
$manifest = Replace-Token $manifest "__IDENTITY_NAME__" $IdentityName
$manifest = Replace-Token $manifest "__PUBLISHER__" $Publisher
$manifest = Replace-Token $manifest "__VERSION__" $Version
$manifest = Replace-Token $manifest "__ARCH__" $Architecture
$manifest = Replace-Token $manifest "__DISPLAY_NAME__" $DisplayName
$manifest = Replace-Token $manifest "__PUBLISHER_DISPLAY_NAME__" $PublisherDisplayName
$manifest = Replace-Token $manifest "__DESCRIPTION__" $Description
Set-Content -Path (Join-Path $stageAbs "AppxManifest.xml") -Value $manifest -Encoding UTF8

$makeAppx = Require-Tool "makeappx.exe"
if (Test-Path $outputAbs) {
    Remove-Item -Path $outputAbs -Force
}
New-Item -ItemType Directory -Path (Split-Path -Parent $outputAbs) -Force | Out-Null

& $makeAppx pack /d $stageAbs /p $outputAbs /o

if ($SkipSign) {
    Write-Host "MSIX created but not signed: $outputAbs"
    exit 0
}

$signtool = Require-Tool "signtool.exe"
if (-not [string]::IsNullOrWhiteSpace($PfxPath)) {
    if ([string]::IsNullOrWhiteSpace($PfxPassword)) {
        throw "PfxPassword must be provided with PfxPath."
    }
    & $signtool sign /fd SHA256 /f $PfxPath /p $PfxPassword /tr http://timestamp.digicert.com /td SHA256 $outputAbs
} elseif (-not [string]::IsNullOrWhiteSpace($CertThumbprint)) {
    & $signtool sign /fd SHA256 /sha1 $CertThumbprint /tr http://timestamp.digicert.com /td SHA256 $outputAbs
} else {
    throw "Provide either PfxPath (+ PfxPassword) or CertThumbprint for signing."
}

Write-Host "Signed MSIX created: $outputAbs"
