<#
.SYNOPSIS
    Build the SoftVoice SAPI 5 package: both interfaces, the utility, the
    tools and the installer.

.DESCRIPTION
    Configures and builds the project twice - once for Win32 and once for x64
    - stages the result into output\, and compiles the Inno Setup installer
    into dist\.

    The two SAPI 5 interfaces are built from identical sources. The engine is
    a 32-bit 1997 DLL that runs in its own process on every architecture, so
    neither interface loads it and there is no bitness-specific code path to
    keep in step.

.PARAMETER Version
    Version stamped into the installer and its filename.

.PARAMETER SkipInstaller
    Build and stage, but do not run the Inno Setup compiler.

.PARAMETER Probe
    Also build the accessibility probe installer: the same wizard with no
    payload and no elevation, for checking the pages with a screen reader.

.EXAMPLE
    pwsh -File tools\build_all.ps1
#>
[CmdletBinding()]
param(
    [string]$Version = "1.0.0",
    [switch]$SkipInstaller,
    [switch]$Probe
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Step($text) {
    Write-Host ""
    Write-Host "==> $text" -ForegroundColor Cyan
}

function Fail($text) {
    Write-Host "ERROR: $text" -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------- prereqs ---

Step "Checking the toolchain"

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) { Fail "cmake is not on PATH." }
Write-Host "  cmake:      $($cmake.Source)"

$iscc = $null
$isccCandidates = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
)
foreach ($candidate in $isccCandidates) {
    if (Test-Path $candidate) { $iscc = $candidate; break }
}
if ($iscc) {
    Write-Host "  Inno Setup: $iscc"
} elseif (-not $SkipInstaller) {
    Fail "ISCC.exe was not found. Install Inno Setup 6, or pass -SkipInstaller."
}

# The engine payload has to be here; without it nothing speaks.
$engineFiles = @("svwebspeak-host.exe", "SVctl32.DLL", "SVENG32.DLL",
                 "Svspan32.dll")
foreach ($file in $engineFiles) {
    if (-not (Test-Path (Join-Path $root "bin\$file"))) {
        Fail "bin\$file is missing. The SoftVoice engine cannot be packaged without it."
    }
}
Write-Host "  engine:     all $($engineFiles.Count) files present in bin\"

# ------------------------------------------------------------------ build ---

foreach ($arch in @("Win32", "x64")) {
    $buildDir = if ($arch -eq "Win32") { "build_x86" } else { "build_x64" }

    Step "Configuring $arch"
    & cmake -S . -B $buildDir -G "Visual Studio 17 2022" -A $arch | Out-Host
    if ($LASTEXITCODE -ne 0) { Fail "CMake configuration failed for $arch." }

    Step "Building $arch"
    & cmake --build $buildDir --config Release | Out-Host
    if ($LASTEXITCODE -ne 0) { Fail "Build failed for $arch." }
}

# ------------------------------------------------------------------ stage ---

Step "Staging output\"

$output = Join-Path $root "output"
if (Test-Path $output) { Remove-Item -Recurse -Force $output }
New-Item -ItemType Directory -Force -Path $output | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $output "x64") | Out-Null

$x86 = Join-Path $root "build_x86\bin\Release"
$x64 = Join-Path $root "build_x64\bin\Release"

# 32-bit: the interface for 32-bit applications, the utility, and the tools.
foreach ($file in @("SoftVoiceSAPI.dll", "SoftVoiceConfig.exe",
                    "sv_render.exe", "sv_speak.exe", "sv_selftest.exe",
                    "sv_latency.exe")) {
    Copy-Item (Join-Path $x86 $file) $output -Force
}

# 64-bit: the interface for 64-bit applications, and the same tools so each
# architecture can be exercised on its own.
foreach ($file in @("SoftVoiceSAPI.dll", "sv_render.exe", "sv_speak.exe",
                    "sv_selftest.exe", "sv_latency.exe")) {
    Copy-Item (Join-Path $x64 $file) (Join-Path $output "x64") -Force
}

# The engine and its 32-bit host, shared by both interfaces.
foreach ($file in $engineFiles) {
    Copy-Item (Join-Path $root "bin\$file") $output -Force
}

Copy-Item (Join-Path $root "installer\open_logs.cmd") $output -Force
Copy-Item (Join-Path $root "installer\before_install.txt") $output -Force

$staged = (Get-ChildItem -Recurse -File $output).Count
Write-Host "  $staged files staged in output\"

# ---------------------------------------------------------------- verify ----

Step "Verifying the staged build"

# Speaking here is what proves the package works before it is wrapped up.
& (Join-Path $output "sv_selftest.exe") --dll (Join-Path $output "SoftVoiceSAPI.dll") | Out-Host
if ($LASTEXITCODE -ne 0) { Fail "The 32-bit SAPI 5 self-test failed." }

& (Join-Path $output "x64\sv_selftest.exe") --dll (Join-Path $output "x64\SoftVoiceSAPI.dll") | Out-Host
if ($LASTEXITCODE -ne 0) { Fail "The 64-bit SAPI 5 self-test failed." }

# ------------------------------------------------------------- installer ----

if ($SkipInstaller) {
    Step "Done (installer skipped)"
    Write-Host "  output\  $output"
    exit 0
}

Step "Compiling the installer"

$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null

$iss = Join-Path $root "installer\softvoice_sapi5.iss"
& $iscc "/DStageDir=$output" "/DVersion=$Version" $iss | Out-Host
if ($LASTEXITCODE -ne 0) { Fail "The Inno Setup compiler failed." }

if ($Probe) {
    Step "Compiling the accessibility probe installer"
    & $iscc "/DStageDir=$output" "/DVersion=$Version" "/DProbe" $iss | Out-Host
    if ($LASTEXITCODE -ne 0) { Fail "The probe installer failed to compile." }
}

Step "Done"
Get-ChildItem $dist -Filter *.exe | ForEach-Object {
    Write-Host ("  {0}  {1:N1} MB" -f $_.Name, ($_.Length / 1MB))
}
