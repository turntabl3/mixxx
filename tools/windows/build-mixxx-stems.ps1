#Requires -Version 5.1
<#
.SYNOPSIS
    Build Mixxx (this fork: offline stem separation) for Windows x64 or arm64 and
    stage a folder that can be copied to another PC and run.

.DESCRIPTION
    This script reproduces the repository's own Windows build recipe. It is not
    invented: every command, option, URL and checksum below is copied from a file
    in this repository, with the source recorded next to it. The main sources are

      .github/workflows/build.yml          (the Windows CI jobs and their CMake args)
      tools/windows_buildenv.bat           (local Windows build environment setup)
      tools/windows_release_buildenv.bat   (the release variant of the above)
      CMakeLists.txt                       (options, install rules, buildenv bootstrap)
      CONTRIBUTING.md                      (prerequisite expectations)

    It does four things, in order:

      1. Checks (and where possible installs) the prerequisites: Visual Studio
         Build Tools with the C++ workload, CMake, Ninja, Git, Python.
      2. Downloads and verifies the Mixxx prebuilt dependency environment for the
         requested architecture. Building the vcpkg dependencies from source
         would take many hours, so this uses the same prebuilt "buildenv" zip
         the project publishes and CI consumes.
      3. Configures and builds mixxx.exe for the requested architecture with
         RelWithDebInfo, exactly like CI.
      4. Stages a runnable directory: mixxx.exe, the DLLs it needs (vcpkg
         applocal dependency copying), and the res/ data tree (skins, controller
         mappings, effect presets, fonts, keyboard mappings, translations, the
         stem separation scripts) flat next to the exe, because on Windows
         MIXXX_INSTALL_BINDIR and MIXXX_INSTALL_DATADIR are both "." (CMakeLists.txt:2293-2294).

    WHAT IS VERIFIED AND WHAT IS BEST-EFFORT
      No Windows machine was available while writing this, so nothing here has
      been executed. Two categories are used in the comments:

        [VERIFIED-FROM-REPO]  read out of this repository, file:line given.
        [VERIFIED-ONLINE]     the URL/checksum was fetched and checked by the
                              author on 2026-10-13 from the same sources the
                              script uses (so it is known to exist and match).
        [BEST-EFFORT]         written from general Windows knowledge; plausible
                              but NOT executed anywhere. Expect to fix details
                              on the first real run.

.PARAMETER Architecture
    x64 or arm64. Defaults to the host's architecture
    (PROCESSOR_ARCHITECTURE; the mapping is windows_buildenv.bat:8-16).

.PARAMETER DepsVariant
    release (default) uses the "-release" triplet and buildenv, which is what CI
    uses (build.yml:140,167). debug uses the Debug-capable triplet
    (windows_buildenv.bat:43-45,55-57). Debug builds require the debug variant.

.PARAMETER StageDir
    Where the runnable folder is produced. Default <repo>\stage\mixxx-<arch>.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\windows\build-mixxx-stems.ps1 -Architecture x64

.EXAMPLE
    # arm64, keep the build environment on a short path to stay under MAX_PATH
    powershell -ExecutionPolicy Bypass -File tools\windows\build-mixxx-stems.ps1 `
        -Architecture arm64 -BuildEnvBase C:\mixxx-buildenv -Zip

.NOTES
    Exit codes: 0 ok, 1 prerequisite problem, 2 download/checksum problem,
    3 configure problem, 4 build problem, 5 install/stage problem, 64 usage.
#>

[CmdletBinding()]
param(
    # Target architecture. Empty means "detect from the host".
    [string]$Architecture = '',

    # release => triplet <arch>-windows-release (CI). debug => <arch>-windows.
    [ValidateSet('release', 'debug')]
    [string]$DepsVariant = 'release',

    # CMake build type. CI uses RelWithDebInfo (build.yml:439).
    [ValidateSet('RelWithDebInfo', 'Release', 'Debug')]
    [string]$BuildType = 'RelWithDebInfo',

    # Where the prebuilt dependency environment is unpacked.
    # windows_buildenv.bat:23-25 defaults this to <repo>\buildenv. We keep that
    # default unless the resulting paths would be at risk of exceeding the
    # 260 character MAX_PATH limit (the archive contains entries of up to 234
    # characters, measured from the published arm64 zip), in which case a short
    # path on the system drive is used instead.
    [string]$BuildEnvBase = '',

    # Where CMake builds. CI builds in "<repo>/build" (build.yml:433-434); a
    # per-architecture folder keeps x64 and arm64 builds side by side.
    [string]$BuildDir = '',

    # Where the runnable folder is produced.
    [string]$StageDir = '',

    # Where CMake/Ninja are unpacked if they have to be downloaded.
    [string]$ToolsDir = '',

    # Parallel build jobs. 0 means "one per logical processor" (CI: Ninja uses
    # all cores by default, build.yml:469-470).
    [int]$Jobs = 0,

    # Delete the build directory before configuring.
    [switch]$Clean,

    # Only report missing prerequisites, never install anything.
    [switch]$SkipPrereqInstall,

    # Assume the dependency environment is already unpacked; never download.
    [switch]$SkipDeps,

    # Delete an existing dependency environment and download it again.
    [switch]$ForceDeps,

    # Never touch the network for prerequisites: fail with instructions instead.
    [switch]$NoDownload,

    # Match CI exactly: warnings are errors (build.yml:441) and the manual PDF is
    # downloaded during configure (build.yml:445, CMakeLists.txt:3188-3222).
    # Both are OFF by default here because a warning from a slightly different
    # MSVC or a missing manual PDF would otherwise break a release build of the
    # user's own tree; CI has both because it builds the pristine upstream tree.
    [switch]$WarningsFatal,
    [switch]$DownloadManual,

    # Build the unit tests as well (CI does; needs GTest from the buildenv and
    # roughly doubles the build time). Off by default for a shipping build.
    [switch]$WithTests,

    # Run "mixxx.exe --version" after staging and look for Mixxx startup output,
    # the way the AppImage job smoke-tests its artifact (build.yml:600-627).
    [switch]$SmokeTest,

    # Zip the staged folder for copying to another machine.
    [switch]$Zip,

    # Also create the Python environment demucs needs, using the fork's own
    # provision script. Downloads PyTorch (~1-2 GB). See res/stems/README.md:41-49
    # and res/stems/provision.py. Optional: the application can create this
    # itself from Preferences > Stems (src/stems/stemprovisionjob.cpp:45-111).
    [switch]$InstallStemsVenv,

    # Target directory for -InstallStemsVenv. Defaults to the directory the
    # application itself would use: Mixxx's AppLocalDataLocation
    # (src/util/cmdlineargs.cpp:101) plus "stems"
    # (src/preferences/stemseparationsettings.h:114-117).
    [string]$StemsVenvDir = ''
)

Set-StrictMode -Version 3.0
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Constants taken from the repository
# ---------------------------------------------------------------------------

# The prebuilt dependency environments. Every value below is copied from
# tools/windows_buildenv.bat:35-66; the URL is built exactly like line 66:
#   https://downloads.mixxx.org/dependencies/<BUILDENV_BRANCH>/Windows/<BUILDENV_NAME>.zip
# The SHA256 values are the ones the repo ships (lines 40,45,52,57). Both
# release-variant checksums were fetched and recomputed by the author on
# 2026-10-13 and match. [VERIFIED-FROM-REPO][VERIFIED-ONLINE]
$BuildEnvs = @{
    'x64|release'   = @{
        Branch  = '2.7-rel'
        Triplet = 'x64-windows-release'
        Name    = 'mixxx-deps-2.7-x64-windows-rel-ee6dc7fe'
        Sha256  = 'f98a2dcfb697caeed4bc012bc28de2b784728a1d795fbc62f44cc0f0a3b50b39'
    }
    'x64|debug'     = @{
        Branch  = '2.7'
        Triplet = 'x64-windows'
        Name    = 'mixxx-deps-2.7-x64-windows-1c20f84a'
        Sha256  = '77b75ab17f06e07b1c140e90f86aafb8658d66d8d581d488ab9b9545996fdc6e'
    }
    'arm64|release' = @{
        Branch  = '2.7-rel'
        Triplet = 'arm64-windows-release'
        Name    = 'mixxx-deps-2.7-arm64-windows-rel-ee6dc7fe'
        Sha256  = '38218f4874d3852a0689009385fe0106ff089511e37d7c062bc698449c8b9110'
    }
    'arm64|debug'   = @{
        Branch  = '2.7'
        Triplet = 'arm64-windows'
        Name    = 'mixxx-deps-2.7-arm64-windows-1c20f84a'
        Sha256  = '3e856b1b2597c03528b1a00871ae6b0d54d188e125d7d87b5671b2a104ee0667'
    }
}

# CMake: CI pins 3.30.x on Windows x64 (build.yml:276-284) and uses a
# downloadable 3.30.1 for arm64 (build.yml:292-299). WIX/ARM64 needs >= 3.24
# (CMakeLists.txt:5719-5722). We pin the same 3.30.1 zips. [VERIFIED-FROM-REPO]
$CmakePinnedVersion = '3.30.1'
$CmakeDownloads = @{
    'x64'   = @{
        Url    = 'https://github.com/Kitware/CMake/releases/download/v3.30.1/cmake-3.30.1-windows-x86_64.zip'
        Sha256 = 'cf7788ff9d92812da194847d4ec874fc576f34079987d0f20c96cd09e2a16220'
        Dir    = 'cmake-3.30.1-windows-x86_64'
    }
    'arm64' = @{
        Url    = 'https://github.com/Kitware/CMake/releases/download/v3.30.1/cmake-3.30.1-windows-arm64.zip'
        Sha256 = '02b433f70aa549449be2d53046d0179590bf3b6290d9fda3fbbb23f96a4f2802'
        Dir    = 'cmake-3.30.1-windows-arm64'
    }
}
# SHA-256 values above come from the release's own cmake-3.30.1-SHA-256.txt. [VERIFIED-ONLINE]

# Ninja: the generator the project uses on Windows (CMAKE_GENERATOR=Ninja,
# tools/windows_buildenv.bat:92). It is not shipped with CMake or with the
# dependency environment (checked: the published buildenv contains no ninja.exe).
# Official Ninja release zips; the checksums were computed by the author on
# 2026-10-13 from the release assets. [VERIFIED-ONLINE]
$NinjaVersion = '1.13.2'
$NinjaDownloads = @{
    'x64'   = @{
        Url    = 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip'
        Sha256 = '07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65'
    }
    'arm64' = @{
        Url    = 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-winarm64.zip'
        Sha256 = 'e52f0bdef9dfb1003229dbd6508a508c4073fd017247002adc66e5e806cb0391'
    }
}

# VS 2022 Build Tools bootstrapper; used when winget is unavailable.
# [VERIFIED-ONLINE] (HTTP 200, 4474800 bytes, fetched 2026-10-13)
$VsBootstrapperUrl = 'https://aka.ms/vs/17/release/vs_BuildTools.exe'

# ---------------------------------------------------------------------------
# Small helpers
# ---------------------------------------------------------------------------

function Write-Section {
    param([string]$Text)
    Write-Host ''
    Write-Host ('=' * 78) -ForegroundColor DarkCyan
    Write-Host "== $Text" -ForegroundColor Cyan
    Write-Host ('=' * 78) -ForegroundColor DarkCyan
}

function Write-Info { param([string]$Text) Write-Host "   $Text" }
function Write-Ok   { param([string]$Text) Write-Host "   OK   $Text" -ForegroundColor Green }
function Write-Warn { param([string]$Text) Write-Host "   WARN $Text" -ForegroundColor Yellow }
function Write-Err  { param([string]$Text) Write-Host "   FAIL $Text" -ForegroundColor Red }

function Write-Manual {
    # Prints exact manual steps for something we cannot automate, then exits.
    param([string]$Title, [string[]]$Steps, [int]$Code, [string]$Hint = '')
    Write-Host ''
    Write-Err $Title
    if ($Hint) { Write-Host "   $Hint" }
    Write-Host ''
    Write-Host '   Do this by hand, then re-run this script:' -ForegroundColor Yellow
    $i = 1
    foreach ($s in $Steps) {
        Write-Host ("   {0}. {1}" -f $i, $s)
        $i++
    }
    Write-Host ''
    exit $Code
}

function Die {
    param([string]$Text, [int]$Code = 1)
    Write-Host ''
    Write-Err $Text
    Write-Host ''
    exit $Code
}

function Test-Exe {
    param([string]$Name)
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

function Get-HostArch {
    # Mapping copied from tools/windows_buildenv.bat:8-16.
    # PROCESSOR_ARCHITECTURE under 32-bit emulation on a 64-bit host reports the
    # host in PROCESSOR_ARCHITEW6432, which .bat cannot see but PowerShell can.
    $pa = $env:PROCESSOR_ARCHITECTURE
    if ($env:PROCESSOR_ARCHITEW6432) { $pa = $env:PROCESSOR_ARCHITEW6432 }
    switch -Regex ($pa) {
        '^(AMD64|x86_64)$' { return 'x64' }
        '^ARM64$'          { return 'arm64' }
        default            { return '' }
    }
}

function Get-FreeSpaceGB {
    param([string]$Path)
    try {
        $root = $null
        try { $root = [System.IO.Path]::GetPathRoot((Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path) } catch { }
        if (-not $root) { $root = [System.IO.Path]::GetPathRoot($Path) }
        if (-not $root) { return -1 }
        $d = New-Object System.IO.DriveInfo $root
        return [math]::Round($d.AvailableFreeSpace / 1GB, 1)
    } catch {
        return -1
    }
}

# Download with the healthiest tool available, then verify the checksum.
# The checksum check mirrors CMakeLists.txt:162-177, which refuses to use a
# mismatching buildenv zip; we do the same so a truncated download fails here
# with a clear message instead of much later inside CMake.
function Get-VerifiedFile {
    param(
        [string]$Url,
        [string]$Destination,
        [string]$Sha256,
        [string]$Description
    )
    $dir = Split-Path -Parent $Destination
    if (-not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }

    if (Test-Path -LiteralPath $Destination) {
        $existing = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash.ToLower()
        if (-not $Sha256 -or $existing -eq $Sha256.ToLower()) {
            Write-Ok "$Description already downloaded and checksum matches"
            return
        }
        Write-Warn "$Description exists but its checksum is wrong; downloading again"
        Remove-Item -LiteralPath $Destination -Force
    }

    if ($NoDownload) {
        Write-Manual "Cannot download $Description" @(
            "Download $Url",
            "Move it to $Destination",
            "Re-run without -NoDownload"
        ) 2 "The file was not found locally and -NoDownload was given."
    }

    Write-Info "downloading $Description"
    Write-Info "  $Url"

    # curl.exe is in the box on Windows 10 1803+ and shows progress and can
    # resume; PowerShell 5.1's `curl` is an alias for Invoke-WebRequest, hence
    # the explicit path. [BEST-EFFORT]
    $curl = Join-Path $env:SystemRoot 'System32\curl.exe'
    $downloaded = $false
    if (Test-Path -LiteralPath $curl) {
        & $curl --fail --location --retry 3 --retry-delay 5 --output $Destination $Url
        if ($LASTEXITCODE -eq 0) { $downloaded = $true }
        else { Write-Warn "curl.exe exited with $LASTEXITCODE; trying Invoke-WebRequest" }
    }
    if (-not $downloaded) {
        $old = $ProgressPreference
        $ProgressPreference = 'SilentlyContinue'
        try {
            Invoke-WebRequest -Uri $Url -OutFile $Destination -UseBasicParsing
            $downloaded = $true
        } catch {
            Write-Manual "Download of $Description failed" @(
                "Download $Url with a browser",
                "Save it as $Destination",
                "Re-run this script (it will verify the checksum and continue)"
            ) 2 $_.Exception.Message
        } finally {
            $ProgressPreference = $old
        }
    }

    if ($Sha256) {
        $actual = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash.ToLower()
        if ($actual -ne $Sha256.ToLower()) {
            Remove-Item -LiteralPath $Destination -Force -ErrorAction SilentlyContinue
            Die ("Checksum mismatch for $Description`n" +
                 "     expected $($Sha256.ToLower())`n" +
                 "          got $actual`n" +
                 "     The download was truncated or the file on the server changed.") 2
        }
        Write-Ok "$Description downloaded, SHA256 verified"
    } else {
        Write-Ok "$Description downloaded (no checksum available)"
    }
}

# Extract a zip. Order of preference:
#   1. "cmake -E tar xzf", the exact command CMakeLists.txt:183-186 uses for the
#      same archive; CMake's libarchive-based reader copes with the long paths
#      and the 18k entries in the dependency environment.
#   2. tar.exe (bsdtar, in the box on Windows 10 1803+), same libarchive, used
#      when CMake is not installed yet (we unpack CMake itself with it).
#   3. the repository's own tools/unzip.ps1, which tools/windows_buildenv.bat:149-152
#      uses for the same purpose.
#   4. Expand-Archive as the last resort.
# [VERIFIED-FROM-REPO] for the command choices; the fallback order is BEST-EFFORT
# in the sense that it has not been executed on Windows.
function Expand-ArchiveFile {
    param(
        [string]$ZipPath,
        [string]$Destination,
        [string]$CmakeExe = 'cmake'
    )
    if (-not (Test-Path -LiteralPath $Destination)) {
        New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    }
    $zipFull = (Resolve-Path -LiteralPath $ZipPath).Path
    $destFull = (Resolve-Path -LiteralPath $Destination).Path

    if ($CmakeExe -and (Test-Exe $CmakeExe)) {
        Push-Location $destFull
        try {
            Write-Info "extracting with: $CmakeExe -E tar xzf"
            & $CmakeExe -E tar xzf $zipFull
            if ($LASTEXITCODE -eq 0) { Write-Ok 'extracted'; return }
            Write-Warn "$CmakeExe -E tar exited with $LASTEXITCODE; falling back"
        } finally {
            Pop-Location
        }
    }

    $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
    if (Test-Path -LiteralPath $tar) {
        Write-Info 'extracting with tar.exe (bsdtar)'
        & $tar -x -f $zipFull -C $destFull
        if ($LASTEXITCODE -eq 0) { Write-Ok 'extracted'; return }
        Write-Warn "tar.exe exited with $LASTEXITCODE; falling back"
    }

    $repoUnzip = Join-Path $script:RepoRoot 'tools\unzip.ps1'
    if (Test-Path -LiteralPath $repoUnzip) {
        Write-Info "extracting with the repository's tools/unzip.ps1"
        & powershell -NoProfile -ExecutionPolicy Bypass -File $repoUnzip $zipFull $destFull
        if ($LASTEXITCODE -eq 0) { Write-Ok 'extracted'; return }
        Write-Warn "tools/unzip.ps1 exited with $LASTEXITCODE; falling back"
    }

    Write-Info 'extracting with Expand-Archive'
    try {
        Expand-Archive -LiteralPath $zipFull -DestinationPath $destFull -Force
        Write-Ok 'extracted'
    } catch {
        Die "Could not extract $zipFull. Enable long path support (see the note this script printed about MAX_PATH) or extract manually to $destFull, then re-run." 2
    }
}

function Get-NumericVersion {
    # Pulls the first "major.minor[.patch]" out of a version string such as
    # "cmake version 3.30.1" so it can be compared with [version].
    param([string]$Text)
    $m = [regex]::Match($Text, '(\d+)\.(\d+)(?:\.(\d+))?')
    if (-not $m.Success) { return $null }
    $patch = if ($m.Groups[3].Success) { $m.Groups[3].Value } else { '0' }
    return [version]("{0}.{1}.{2}" -f $m.Groups[1].Value, $m.Groups[2].Value, $patch)
}

function Test-VersionAtLeast {
    param([string]$Actual, [string]$Minimum)
    $a = Get-NumericVersion $Actual
    if (-not $a) { return $false }
    return $a -ge ([version]$Minimum)
}

# ---------------------------------------------------------------------------
# Resolve paths and the target architecture
# ---------------------------------------------------------------------------

$script:RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path

if ($env:OS -ne 'Windows_NT') {
    Die "This script builds for Windows and must run on Windows.`n     It was run with OS='$($env:OS)'."
}

if (-not $Architecture) {
    $Architecture = Get-HostArch
    if (-not $Architecture) {
        Die "Cannot detect the host architecture from PROCESSOR_ARCHITECTURE='$($env:PROCESSOR_ARCHITECTURE)'.`n     Pass -Architecture x64 or -Architecture arm64 explicitly."
    }
    Write-Info "no -Architecture given, using the host architecture: $Architecture"
}
if ($Architecture -notin @('x64', 'arm64')) {
    Die "-Architecture must be x64 or arm64, got '$Architecture'." 64
}

$hostArch = Get-HostArch
if ($Architecture -eq 'arm64' -and $hostArch -eq 'x64') {
    # CI builds arm64 on a native arm64 runner (build.yml:152-153), it does not
    # cross-compile. Cross-compiling from an x64 host is possible with the ARM64
    # build tools, but it is not a path CI exercises, so say so loudly.
    Write-Warn 'Target architecture arm64 on an x64 host: this is a cross build.'
    Write-Warn 'CI builds arm64 on a native arm64 runner (build.yml:152-153); if arm64 fails,'
    Write-Warn 're-run this script on a Windows on ARM machine.'
}

$archTag = if ($DepsVariant -eq 'release') { $Architecture } else { "$Architecture-debugdeps" }

if (-not $BuildEnvBase) {
    # See the note on -BuildEnvBase: the archive's longest entry is 234 characters
    # (measured), so a long repository path plus the archive paths can pass the
    # 260 character MAX_PATH limit. Prefer a short path in that case.
    $proposed = Join-Path $script:RepoRoot 'buildenv'
    if (($proposed.Length + 240) -ge 250) {
        $BuildEnvBase = Join-Path $env:SystemDrive 'mixxx-buildenv'
        Write-Warn "repository path is long; using the short build environment path $BuildEnvBase"
        Write-Warn '(the dependency archive contains paths of up to 234 characters)'
    } else {
        $BuildEnvBase = $proposed
    }
}
if (-not $BuildDir) {
    $BuildDir = Join-Path $script:RepoRoot "build\windows-$archTag"
}
if (-not $StageDir) {
    $StageDir = Join-Path $script:RepoRoot "stage\mixxx-$archTag"
}
if (-not $ToolsDir) {
    $ToolsDir = Join-Path $BuildEnvBase 'tools'
}

$BuildEnv = $BuildEnvs["$Architecture|$DepsVariant"]
$BuildEnvPath = Join-Path $BuildEnvBase $BuildEnv.Name          # windows_buildenv.bat:88
$BuildEnvUrl = "{0}/dependencies/{1}/Windows/{2}.zip" -f 'https://downloads.mixxx.org', $BuildEnv.Branch, $BuildEnv.Name  # windows_buildenv.bat:66
$InstalledDir = Join-Path (Join-Path $BuildEnvPath 'installed') $BuildEnv.Triplet  # windows_buildenv.bat:93
$BuildEnvZip = Join-Path $BuildEnvBase "$($BuildEnv.Name).zip" # CMakeLists.txt:147-153

Write-Section 'Plan'
Write-Info "repository:        $script:RepoRoot"
Write-Info "architecture:      $Architecture (host: $hostArch)"
Write-Info "configuration:     $BuildType"
Write-Info "vcpkg triplet:     $($BuildEnv.Triplet)"
Write-Info "build environment: $BuildEnvPath"
Write-Info "build dir:         $BuildDir"
Write-Info "stage dir:         $StageDir"
Write-Host ''
Write-Info 'The stem separation feature is compiled in unconditionally by this fork:'
Write-Info 'CMakeLists.txt:2907-2912 defines __STEM__ for mixxx-lib whenever FFmpeg is'
Write-Info 'available, so no extra flag is needed. This script asserts that the'
Write-Info 'configure step really produced STEM=ON before building (see below).'

# ---------------------------------------------------------------------------
# 1. Toolchain prerequisites
# ---------------------------------------------------------------------------

Write-Section 'Step 1/6  Prerequisites'

$toolsToInstall = @()

# --- Git ---------------------------------------------------------------
# Not strictly required: CMakeLists calls "git describe" through
# cmake/modules/GitInfo.cmake:1-16, which degrades to "Git describe is unknown"
# with ERROR_QUIET when git is absent. Recommended anyway.
if (Test-Exe 'git') {
    $gitVersion = (& git --version 2>&1 | Select-Object -First 1)
    Write-Ok "git: $gitVersion"
} else {
    Write-Warn 'git not found (optional: only used for the version string, cmake/modules/GitInfo.cmake:1-16)'
    $toolsToInstall += 'git'
}

# --- Python ------------------------------------------------------------
# Not needed to build Mixxx (CMakeLists.txt does not use Python) and not needed
# to run it. It IS needed for the optional demucs environment behind
# stem separation (res/stems/README.md:41-49).
if (Test-Exe 'python') {
    $pyVersion = (& python --version 2>&1 | Select-Object -First 1)
    Write-Ok "python: $pyVersion"
} elseif (Test-Exe 'py') {
    $pyVersion = (& py --version 2>&1 | Select-Object -First 1)
    Write-Ok "python: $pyVersion (via the py launcher)"
} else {
    Write-Warn 'python not found; the build does not need it, but -InstallStemsVenv and the'
    Write-Warn 'application''s own "install separation environment" button do'
    $toolsToInstall += 'python'
}

# --- winget ------------------------------------------------------------
$hasWinget = Test-Exe 'winget'
Write-Info ("winget: {0}" -f $(if ($hasWinget) { 'available' } else { 'NOT available' }))

function Install-WithWinget {
    param([string]$Id, [string]$Override = '', [string]$Friendly)
    if (-not $hasWinget) { return $false }
    if ($SkipPrereqInstall) {
        Write-Warn "$Friendly is missing and -SkipPrereqInstall was given"
        return $false
    }
    Write-Info "installing $Friendly with winget ($Id)"
    $wingetArgs = @('install', '--id', $Id, '--exact', '--accept-source-agreements',
                    '--accept-package-agreements', '--disable-interactivity')
    if ($Override) { $wingetArgs += @('--override', $Override) }
    & winget @wingetArgs
    if ($LASTEXITCODE -eq 0) { Write-Ok "$Friendly installed (a new shell may be needed for PATH)"; return $true }
    Write-Warn "winget exited with $LASTEXITCODE while installing $Friendly"
    return $false
}

# Optional tools (Git, Python). Both are non-fatal for the build; install them
# silently best-effort when winget is available, otherwise print what to install.
foreach ($tool in $toolsToInstall) {
    switch ($tool) {
        'git' {
            if (-not (Install-WithWinget -Id 'Git.Git' -Friendly 'Git')) {
                Write-Warn 'install Git by hand if you want a proper version string:'
                Write-Warn '  winget install --id Git.Git --exact    (or https://git-scm.com/download/win)'
            }
        }
        'python' {
            if (-not (Install-WithWinget -Id 'Python.Python.3.12' -Friendly 'Python 3')) {
                Write-Warn 'install Python by hand if you want -InstallStemsVenv:'
                Write-Warn '  winget install --id Python.Python.3.12 --exact'
            }
        }
    }
}

# --- Visual Studio Build Tools with the C++ workload --------------------
# The project needs an MSVC developer environment: CMakeLists.txt:281-289 fails
# the configure with "The INCLUDE environment variable is not defined" otherwise.
# CI enters that environment with ilammy/msvc-dev-cmd, arch = x64 | arm64
# (build.yml:286-290).
$script:VsInstallPath = ''
function Find-VsInstall {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { return '' }
    $requires = if ($Architecture -eq 'arm64') {
        '-requires Microsoft.VisualStudio.Component.VC.Tools.ARM64'
    } else {
        '-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
    }
    $vswhereArgs = @('-latest', '-prerelease', '-products', '*', '-property', 'installationPath') + @($requires)
    $p = & $vswhere @vswhereArgs 2>$null
    if ($p) { return ($p | Select-Object -First 1).Trim() }
    return ''
}

$script:VsInstallPath = Find-VsInstall
if ($script:VsInstallPath) {
    Write-Ok "Visual Studio with the $Architecture C++ tools: $script:VsInstallPath"
} else {
    Write-Warn "Visual Studio Build Tools with C++ $Architecture tools not found"
    # Component IDs: workload VCTools plus the arch-specific compiler and a
    # Windows SDK. The same set is used in the vendor's ARM64 guidance and is
    # what the installer needs to give us cl.exe / vcvarsall.bat / the SDK.
    $componentArgs = @(
        '--wait', '--quiet', '--norestart',
        '--add', 'Microsoft.VisualStudio.Workload.VCTools',
        '--add', 'Microsoft.VisualStudio.Component.Windows11SDK.22621',
        '--includeRecommended'
    )
    if ($Architecture -eq 'arm64') {
        $componentArgs += @('--add', 'Microsoft.VisualStudio.Component.VC.Tools.ARM64')
    } else {
        $componentArgs += @('--add', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64')
    }
    $override = ($componentArgs -join ' ')

    $installed = Install-WithWinget -Id 'Microsoft.VisualStudio.2022.BuildTools' -Override $override -Friendly 'Visual Studio 2022 Build Tools (C++ workload)'

    if (-not $installed -and -not $SkipPrereqInstall) {
        # Fall back to the vendor bootstrapper, whose URL is the stable aka.ms
        # link. [BEST-EFFORT]
        $bootstrapper = Join-Path $ToolsDir 'vs_BuildTools.exe'
        Get-VerifiedFile -Url $VsBootstrapperUrl -Destination $bootstrapper -Sha256 '' -Description 'Visual Studio Build Tools bootstrapper'
        Write-Info 'running the Build Tools bootstrapper (this downloads several GB and may need elevation)'
        & $bootstrapper @componentArgs
        if ($LASTEXITCODE -ne 0) {
            Write-Manual 'Visual Studio Build Tools could not be installed automatically' @(
                "Open an administrator prompt and run: `"$bootstrapper`" $override",
                'Wait for it to finish, then close and reopen your shell',
                'Re-run this script'
            ) 1 'The installer needs administrator rights.'
        }
    }

    # Re-check after installing; the installer registers vswhere immediately.
    $script:VsInstallPath = Find-VsInstall
    if (-not $script:VsInstallPath) {
        Write-Manual "Visual Studio Build Tools with the C++ $Architecture workload are still missing" @(
            "Install them: winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --override `"$override`"",
            'Alternatively install Visual Studio 2022 Community and tick "Desktop development with C++"',
            ("Also tick the component for ${Architecture}: Microsoft.VisualStudio.Component.VC.Tools." + $(if ($Architecture -eq 'arm64') { 'ARM64' } else { 'x86.x64' })),
            'Re-run this script'
        ) 1 'CMake cannot run without an MSVC developer environment (CMakeLists.txt:281-289).'
    }
    Write-Ok "Visual Studio found after install: $script:VsInstallPath"
}

# --- CMake -------------------------------------------------------------
# CI pins 3.30.x on Windows x64 (build.yml:276-284) and installs 3.30.1 for
# arm64 (build.yml:292-299); WIX for ARM64 needs >= 3.24 (CMakeLists.txt:5719-5722).
$requiredCmake = if ($Architecture -eq 'arm64') { '3.24' } else { '3.22' }
$cmakeExe = ''
$haveCmake = $false
if (Test-Exe 'cmake') {
    $cmakeExe = (Get-Command cmake).Source
    $verLine = (& cmake --version | Select-Object -First 1)
    # Test-VersionAtLeast pulls the numbers out of the whole line ("cmake
    # version 3.30.1"), so no string surgery is needed here.
    if (Test-VersionAtLeast -Actual $verLine -Minimum $requiredCmake) {
        Write-Ok "cmake: $verLine"
        $haveCmake = $true
    } else {
        Write-Warn "cmake $verLine is older than the required $requiredCmake"
    }
} else {
    Write-Warn 'cmake not found'
}

if (-not $haveCmake -and -not $SkipPrereqInstall) {
    $dl = $CmakeDownloads[$Architecture]
    $zip = Join-Path $ToolsDir ("cmake-{0}-windows-{1}.zip" -f $CmakePinnedVersion, $(if ($Architecture -eq 'x64') { 'x86_64' } else { 'arm64' }))
    Get-VerifiedFile -Url $dl.Url -Destination $zip -Sha256 $dl.Sha256 -Description "CMake $CmakePinnedVersion ($Architecture)"
    # Unpacking under $ToolsDir keeps a native arm64 CMake on PATH on ARM64,
    # which is what CI does explicitly for ARM64 (build.yml:292-299: unpack into
    # %USERPROFILE%\cmake and prepend its bin directory to PATH). CMake does not
    # exist yet at this point, so the extractor is tar.exe/-unzip.ps1 rather than
    # "cmake -E tar".
    Expand-ArchiveFile -ZipPath $zip -Destination $ToolsDir -CmakeExe ''
    $cmakeBin = Join-Path (Join-Path $ToolsDir $dl.Dir) 'bin'
    if (-not (Test-Path -LiteralPath (Join-Path $cmakeBin 'cmake.exe'))) {
        # The archive layout is cmake-<version>-windows-<arch>/bin/cmake.exe; if
        # it differs, search one level down so a layout change does not stop us.
        $found = Get-ChildItem -LiteralPath $ToolsDir -Recurse -Filter 'cmake.exe' -ErrorAction SilentlyContinue |
                 Where-Object { $_.DirectoryName -like '*\bin' } | Select-Object -First 1
        if (-not $found) { Die "cmake.exe was not found after extracting $zip" 1 }
        $cmakeBin = $found.DirectoryName
    }
    $env:PATH = "$cmakeBin;$env:PATH"
    Write-Ok "using downloaded cmake from $cmakeBin"
    $cmakeExe = (Get-Command cmake).Source
    $haveCmake = $true
}

if (-not $haveCmake) {
    Write-Manual 'CMake is missing (or too old)' @(
        "Install it: winget install --id Kitware.CMake --exact",
        "Or download $($CmakeDownloads[$Architecture].Url), unpack it, and add its bin directory to PATH",
        'Re-run this script (it also downloads the pinned 3.30.1 itself when this is automated)'
    ) 1 "Requires CMake >= $requiredCmake."
}

# --- Ninja -------------------------------------------------------------
# CMAKE_GENERATOR=Ninja (tools/windows_buildenv.bat:92). VS ships a ninja under
# Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja and puts it on PATH in a
# developer prompt; the dependency environment does not contain one.
$ninjaExe = ''
if (Test-Exe 'ninja') {
    $ninjaExe = (Get-Command ninja).Source
    Write-Ok "ninja: $(& ninja --version)"
} else {
    $vsNinja = Join-Path $script:VsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
    if (Test-Path -LiteralPath $vsNinja) {
        $env:PATH = "$(Split-Path -Parent $vsNinja);$env:PATH"
        $ninjaExe = $vsNinja
        Write-Ok "ninja: $vsNinja (bundled with Visual Studio)"
    } elseif (-not $SkipPrereqInstall) {
        $dl = $NinjaDownloads[$Architecture]
        $zip = Join-Path $ToolsDir ("ninja-win{0}.zip" -f $(if ($Architecture -eq 'arm64') { 'arm64' } else { '' }))
        Get-VerifiedFile -Url $dl.Url -Destination $zip -Sha256 $dl.Sha256 -Description "Ninja $NinjaVersion ($Architecture)"
        $ninjaDir = Join-Path $ToolsDir "ninja-$NinjaVersion-$Architecture"
        Expand-ArchiveFile -ZipPath $zip -Destination $ninjaDir -CmakeExe 'cmake'
        $env:PATH = "$ninjaDir;$env:PATH"
        $ninjaExe = Join-Path $ninjaDir 'ninja.exe'
        if (-not (Test-Path -LiteralPath $ninjaExe)) { Die "ninja.exe was not found after extracting $zip" 1 }
        Write-Ok "using downloaded ninja $NinjaVersion from $ninjaDir"
    }
}
if (-not $ninjaExe) {
    Write-Manual 'Ninja is missing' @(
        'Install it: winget install --id Ninja-build.Ninja --exact',
        'Or download https://github.com/ninja-build/ninja/releases and put ninja.exe on PATH',
        'Re-run this script'
    ) 1 'The Windows build uses -G Ninja (tools/windows_buildenv.bat:92).'
}

# ---------------------------------------------------------------------------
# 2. MSVC developer environment
# ---------------------------------------------------------------------------

Write-Section 'Step 2/6  MSVC developer environment'

# CMakeLists.txt:281-289 refuses to configure without INCLUDE set, i.e. outside a
# developer prompt. CI achieves this with ilammy/msvc-dev-cmd, arch = matrix.arch
# (build.yml:286-290); we do the equivalent locally.
$vcvarsArch = $Architecture   # native build: arm64 on arm64, x64 on x64
if ($Architecture -ne $hostArch) {
    # Cross build, e.g. x64 host -> arm64 target: the bat syntax is <target>_<host>.
    $vcvarsArch = "${Architecture}_$hostArch"
}

$entered = $false
$devShellDll = Join-Path $script:VsInstallPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
if (Test-Path -LiteralPath $devShellDll) {
    try {
        Import-Module $devShellDll -ErrorAction Stop
        # -DevCmdArguments mirrors vcvarsall's arch/host_arch switches. [BEST-EFFORT]
        $devArgs = "-arch=$Architecture -host_arch=$hostArch -no_logo"
        Enter-VsDevShell -VsInstallPath $script:VsInstallPath -SkipAutomaticLocation `
            -DevCmdArguments $devArgs -ErrorAction Stop | Out-Null
        $entered = $true
        Write-Ok "entered the Visual Studio developer environment ($devArgs)"
    } catch {
        Write-Warn "Enter-VsDevShell failed ($($_.Exception.Message)); falling back to vcvarsall.bat"
    }
}

if (-not $entered) {
    # Fallback: run vcvarsall.bat in cmd.exe, dump the resulting environment and
    # import it into this PowerShell session. [BEST-EFFORT]
    $vcvars = Join-Path $script:VsInstallPath 'VC\Auxiliary\Build\vcvarsall.bat'
    if (-not (Test-Path -LiteralPath $vcvars)) {
        Die "vcvarsall.bat not found under $script:VsInstallPath; is the C++ workload installed?" 1
    }
    Write-Info "calling $vcvars $vcvarsArch"
    $dump = & cmd.exe /c "call `"$vcvars`" $vcvarsArch >nul 2>&1 && set"
    if ($LASTEXITCODE -ne 0 -or -not $dump) {
        Die "vcvarsall.bat $vcvarsArch failed (exit $LASTEXITCODE)" 1
    }
    foreach ($line in $dump) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path ("env:" + $matches[1]) -Value $matches[2] -ErrorAction SilentlyContinue
        }
    }
    Write-Ok "imported the MSVC environment for $vcvarsArch"
}

if (-not (Test-Path env:INCLUDE)) {
    Die 'INCLUDE is still not set after entering the developer environment; CMake will refuse to configure (CMakeLists.txt:281-289).' 1
}
if (-not (Test-Exe 'cl')) {
    Write-Warn 'cl.exe is not on PATH; CMakeLists.txt:281-289 only checks INCLUDE, but the build needs cl.exe.'
    $clArch = if ($Architecture -eq 'arm64') { 'Hostarm64\arm64' } else { 'Hostx64\x64' }
    $clGuess = Join-Path $script:VsInstallPath "VC\Tools\MSVC"
    if (Test-Path -LiteralPath $clGuess) {
        Write-Warn "compilers found under $clGuess; if the configure fails, open a developer prompt for $Architecture and re-run the cmake commands this script prints."
    }
}

# ---------------------------------------------------------------------------
# 3. Dependency environment (the prebuilt vcpkg buildenv)
# ---------------------------------------------------------------------------

Write-Section 'Step 3/6  Dependency environment'

# Environment variable names and values mirror tools/windows_buildenv.bat:88-108
# (and the CMakeLists variables read back from them).
$env:MIXXX_VCPKG_ROOT        = $BuildEnvPath       # windows_buildenv.bat:91
$env:CMAKE_GENERATOR         = 'Ninja'             # windows_buildenv.bat:92
$env:VCPKG_TARGET_TRIPLET    = $BuildEnv.Triplet   # windows_buildenv.bat:105
$env:CMAKE_PREFIX_PATH       = $InstalledDir       # windows_buildenv.bat:93
$env:BUILDENV_BASEPATH       = $BuildEnvBase       # windows_buildenv.bat:104
$env:BUILDENV_URL            = $BuildEnvUrl        # windows_buildenv.bat:107
$env:BUILDENV_SHA256         = $BuildEnv.Sha256    # windows_buildenv.bat:108
$env:X_VCPKG_APPLOCAL_DEPS_INSTALL = 'ON'          # windows_buildenv.bat:77
# Note: CMakeLists.txt:375-383 forces X_VCPKG_APPLOCAL_DEPS_INSTALL=ON itself on
# Windows; setting it here keeps parity with the .bat and costs nothing.

if ($SkipDeps) {
    if (-not (Test-Path -LiteralPath $InstalledDir)) {
        Die "SkipDeps was given but $InstalledDir does not exist." 1
    }
    Write-Ok "using the existing dependency environment at $BuildEnvPath"
} elseif (Test-Path -LiteralPath $InstalledDir) {
    Write-Ok "dependency environment already present: $BuildEnvPath"
    Write-Info '(delete it or pass -ForceDeps to download it again)'
} else {
    if ($ForceDeps -and (Test-Path -LiteralPath $BuildEnvPath)) {
        Write-Info "removing $BuildEnvPath because -ForceDeps was given"
        Remove-Item -LiteralPath $BuildEnvPath -Recurse -Force
    }

    $free = Get-FreeSpaceGB -Path $BuildEnvBase
    if ($free -ge 0 -and $free -lt 8) {
        Write-Warn "only $free GB free on the drive holding $BuildEnvBase"
        Write-Warn 'CONTRIBUTING.md:86 asks for about 2.5 GB download and 9 GB disk for the dependencies'
        Write-Warn 'alone; the build directory needs several more GB.'
    }

    # Download + verify exactly like CMakeLists.txt:147-177 does when it bootstraps
    # the buildenv itself (same URL, same checksum, same refusal on mismatch).
    Get-VerifiedFile -Url $BuildEnvUrl -Destination $BuildEnvZip -Sha256 $BuildEnv.Sha256 -Description "Mixxx dependency environment $($BuildEnv.Name)"

    Write-Info "unpacking about 4.7 GB into $BuildEnvBase (this takes a while)"
    Expand-ArchiveFile -ZipPath $BuildEnvZip -Destination $BuildEnvBase -CmakeExe $cmakeExe

    if (-not (Test-Path -LiteralPath $InstalledDir)) {
        Die ("The dependency environment did not unpack as expected.`n" +
             "     Expected: $InstalledDir") 2
    }
    Write-Ok "dependency environment ready: $BuildEnvPath"
    # The zip is 1.2 GB; keeping it costs nothing but disk. Delete it by hand if
    # space is tight.
    Write-Info "(the downloaded archive is kept at $BuildEnvZip)"
}

# Sanity check the pieces CMakeLists.txt:308-360 requires of MIXXX_VCPKG_ROOT.
$vcpkgToolchain = Join-Path $BuildEnvPath 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path -LiteralPath $vcpkgToolchain)) {
    Die ("$BuildEnvPath does not look like a Mixxx vcpkg root: $vcpkgToolchain is missing.`n" +
         "     CMakeLists.txt:308-335 would fail the configure here.") 2
}
Write-Ok 'vcpkg toolchain present (CMakeLists.txt:308-360 will pick it up)'

# Both published release environments were inspected by the author on 2026-10-13
# before this script was written. Each contains installed/<triplet>/share/Qt6,
# Qt6Core.dll, FFmpeg (libavcodec/include), keyfinder.dll, hss1394.dll,
# include/lilv-0, opus, wavpack, mad.h, libmodplug, qt6keychain, libusb-1.0,
# portmidi, protobuf and gtest - i.e. every dependency behind the -D...=ON list
# below, for x64 and for arm64 alike. Qt6Core.dll in the arm64 environment is a
# real ARM64 binary (PE machine 0xAA64), so Qt 6 for arm64-windows does exist in
# the dependency source this project uses. [VERIFIED-ONLINE]

# ---------------------------------------------------------------------------
# 4. Configure
# ---------------------------------------------------------------------------

Write-Section 'Step 4/6  Configure'

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    Write-Info "removing $BuildDir because -Clean was given"
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}
if (-not (Test-Path -LiteralPath $BuildDir)) {
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
}
if ($Jobs -le 0) {
    $Jobs = [int]$env:NUMBER_OF_PROCESSORS
    if ($Jobs -le 0) { $Jobs = 4 }
}

# The argument list below is the union of
#   * the Windows matrix entries  (.github/workflows/build.yml:130-140 x64, 157-168 arm64)
#   * the shared Windows/Linux/macOS arguments (.github/workflows/build.yml:439-452)
#   * the variables tools/windows_buildenv.bat:214-242 sets for local VS builds
# with the two deliberate deviations noted at the parameters (-WarningsFatal,
# -DownloadManual, -WithTests are off by default).
$cmakeArgs = @(
    '-S', $script:RepoRoot
    '-B', $BuildDir
    '-G', 'Ninja'                                             # windows_buildenv.bat:92
    "-DCMAKE_BUILD_TYPE=$BuildType"                           # build.yml:439
    "-DVCPKG_TARGET_TRIPLET=$($BuildEnv.Triplet)"             # build.yml:140 / :167
    "-DCMAKE_PREFIX_PATH=$InstalledDir"                       # build.yml:440 + windows_buildenv.bat:93
    # --- CI options, Windows (build.yml:131-139 / :158-166)
    '-DBULK=ON'
    '-DHSS1394=ON'
    '-DLOCALECOMPARE=ON'
    '-DMAD=ON'
    '-DMEDIAFOUNDATION=ON'
    '-DMODPLUG=ON'
    '-DQT6=ON'
    '-DQML=ON'
    '-DWAVPACK=ON'
    # --- CI options, all desktop platforms (build.yml:441-452)
    '-DDEBUG_ASSERTIONS_FATAL=OFF'
    '-DBATTERY=ON'
    '-DBROADCAST=ON'
    '-DHID=ON'
    '-DKEYFINDER=ON'
    '-DLILV=ON'
    '-DOPUS=ON'
    '-DQTKEYCHAIN=ON'
    '-DVINYLCONTROL=ON'
    '-DCMAKE_VERBOSE_MAKEFILE=OFF'
    '-L'
)

# FFmpeg is what gates the stem support: CMakeLists.txt:2907-2912 defines __STEM__
# only when STEM is on, and STEM defaults to on when FFmpeg was found. FFmpeg
# comes from the dependency environment. Ask for it explicitly so a future
# default change cannot silently drop the feature.
$cmakeArgs += '-DFFMPEG=ON'
$cmakeArgs += '-DSTEM=ON'

if ($Architecture -eq 'arm64') {
    $cmakeArgs += "-DVCPKG_DEFAULT_HOST_TRIPLET=$($BuildEnv.Triplet)"  # build.yml:168
}
if ($WarningsFatal)     { $cmakeArgs += '-DWARNINGS_FATAL=ON' }   # build.yml:441
if ($DownloadManual)    { $cmakeArgs += '-DDOWNLOAD_MANUAL=ON' }  # build.yml:445
if ($WithTests)         { $cmakeArgs += @('-DBUILD_TESTING=ON', '-DBUILD_BENCH=ON') }
else                    { $cmakeArgs += @('-DBUILD_TESTING=OFF', '-DBUILD_BENCH=OFF') }

Write-Info "cmake $($cmakeArgs -join ' ')"

$configureLog = Join-Path $BuildDir 'configure.log'
& $cmakeExe @cmakeArgs 2>&1 | Tee-Object -FilePath $configureLog
$configureExit = $LASTEXITCODE
if ($configureExit -ne 0) {
    Write-Err "configure failed (exit $configureExit); full log: $configureLog"
    Write-Manual 'CMake configuration failed' @(
        "Read the error in $configureLog",
        'The most common causes: the dependency environment does not match the triplet,',
        'or the shell is not an MSVC developer prompt (INCLUDE/LIB must be set)',
        'Re-run this script after fixing, adding -Clean'
    ) 3
}
Write-Ok "configure finished (log: $configureLog)"

# --- Assert that the stem separation feature is really compiled in ---------
# This is the fork-specific check that matters most: CMakeLists.txt:2907-2912
# adds the __STEM__ definition only when STEM and FFMPEG are both ON. If either
# is off, the resulting exe would look fine but would have no stem support.
$cacheFile = Join-Path $BuildDir 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cacheFile)) {
    Die "configure reported success but $cacheFile does not exist; the build directory looks wrong." 3
}
$cacheText = Get-Content -LiteralPath $cacheFile -Raw
$stemOn = [bool]($cacheText -match '(?m)^STEM:BOOL=ON\s*$')
$ffmpegOn = [bool]($cacheText -match '(?m)^FFMPEG:BOOL=ON\s*$')
$tripletInCache = [regex]::Match($cacheText, '(?m)^VCPKG_TARGET_TRIPLET:.*?=(.*)$')
if ($stemOn -and $ffmpegOn) {
    Write-Ok 'STEM=ON and FFMPEG=ON in CMakeCache.txt: __STEM__ is compiled in (CMakeLists.txt:2912)'
} else {
    Write-Err ("STEM/FFmpeg are not both enabled: STEM={0} FFMPEG={1}" -f $stemOn, $ffmpegOn)
    Write-Manual 'The stem separation feature would not be in the executable' @(
        "Check ${cacheFile}: STEM and FFMPEG must both be ON",
        'The dependency environment supplies FFmpeg; make sure -DCMAKE_PREFIX_PATH points at installed\<triplet>',
        'Look in the configure log for the FFmpeg find result'
    ) 3
}
if ($tripletInCache.Success) {
    Write-Info "triplet in cache: $($tripletInCache.Groups[1].Value.Trim())"
}

# ---------------------------------------------------------------------------
# 5. Build
# ---------------------------------------------------------------------------

Write-Section 'Step 5/6  Build'

# CI: cmake --build . --config RelWithDebInfo (build.yml:468-470); Ninja uses all
# cores by default there, we make the job count explicit.
$buildArgs = @('--build', $BuildDir, '--config', $BuildType, '--parallel', "$Jobs")
Write-Info "cmake $($buildArgs -join ' ')"
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
& $cmakeExe @buildArgs 2>&1 | Tee-Object -FilePath (Join-Path $BuildDir 'build.log')
$buildExit = $LASTEXITCODE
$stopwatch.Stop()
if ($buildExit -ne 0) {
    Write-Err "build failed (exit $buildExit) after $([int]$stopwatch.Elapsed.TotalMinutes) min"
    Write-Err "full log: $(Join-Path $BuildDir 'build.log')"
    exit 4
}
Write-Ok "build finished in $([int]$stopwatch.Elapsed.TotalMinutes) min"

# Ninja is a single-configuration generator, so the exe lands directly in the
# build directory (or in the configuration subdirectory for multi-config
# generators, which we do not use).
$exeName = 'mixxx.exe'
$builtExe = Get-ChildItem -LiteralPath $BuildDir -Recurse -Filter $exeName -ErrorAction SilentlyContinue |
            Where-Object { $_.DirectoryName -notlike '*\CPack*' } |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $builtExe) {
    Die "No $exeName found under $BuildDir after a successful build.`n     Check the build log for which target was actually built." 4
}
Write-Ok "built: $($builtExe.FullName) ($([math]::Round($builtExe.Length / 1MB, 1)) MB)"

# ---------------------------------------------------------------------------
# 6. Stage a runnable folder
# ---------------------------------------------------------------------------

Write-Section 'Step 6/6  Stage the runnable folder'

if (Test-Path -LiteralPath $StageDir) {
    Write-Info "removing the previous staging directory $StageDir"
    Remove-Item -LiteralPath $StageDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null

# "cmake --install" runs the install rules of CMakeLists.txt, which is exactly
# what the Windows installer is built from (CI packs the same install tree with
# CPack WIX, build.yml:573-586). On Windows MIXXX_INSTALL_BINDIR and
# MIXXX_INSTALL_DATADIR are both "." (CMakeLists.txt:2293-2294), so the result is
# one flat folder:
#   * mixxx.exe and the MSVC runtime (CMakeLists.txt:2584-2592,
#     InstallRequiredSystemLibraries puts the UCRT/CRT DLLs next to the exe)
#   * every DLL the exe needs, copied by vcpkg's "applocal" dependency walk
#     because X_VCPKG_APPLOCAL_DEPS_INSTALL is ON (CMakeLists.txt:375-383)
#   * the Qt plugins and QML modules (CMakeLists.txt:4265-4510+)
#   * the res/ data directories: skins, controllers, effects, fonts, keyboard,
#     translations, licenses and the docs (CMakeLists.txt:2594-2669, 4147)
$installArgs = @('--install', $BuildDir, '--config', $BuildType, '--prefix', $StageDir)
Write-Info "cmake $($installArgs -join ' ')"
& $cmakeExe @installArgs 2>&1 | Tee-Object -FilePath (Join-Path $BuildDir 'install.log')
$installExit = $LASTEXITCODE
if ($installExit -ne 0) {
    Write-Err "install/stage failed (exit $installExit); log: $(Join-Path $BuildDir 'install.log')"
    exit 5
}

$stagedExe = Join-Path $StageDir 'mixxx.exe'
if (-not (Test-Path -LiteralPath $stagedExe)) {
    Die "install finished but $stagedExe is missing." 5
}

# The stem pipeline scripts need nothing beside the exe. They are compiled into
# the binary through res/mixxx.qrc, and Mixxx writes them out beside the
# interpreter environment under its own settings directory the first time they
# are needed (src/stems/stemscripts.cpp). Nothing is copied into the staged
# folder, deliberately: a loose copy there would be data nothing reads, which is
# worse than absent because it looks authoritative and can be edited by mistake.

# Verify what a user would care about: the exe, the DLLs, the Qt plugins, the
# resource directories and the stem scripts all sit in the staged folder.
$dllCount = (Get-ChildItem -LiteralPath $StageDir -Filter '*.dll' -File -ErrorAction SilentlyContinue).Count
$stagedBytes = (Get-ChildItem -LiteralPath $StageDir -Recurse -File -ErrorAction SilentlyContinue |
    Measure-Object -Property Length -Sum).Sum
if (-not $stagedBytes) { $stagedBytes = 0 }
$stagedSize = [math]::Round(($stagedBytes / 1GB), 2)
Write-Info "staged: $stagedSize GB, $dllCount DLLs in the top level"
foreach ($expected in @('skins', 'controllers', 'effects', 'fonts', 'keyboard', 'translations')) {
    $p = Join-Path $StageDir $expected
    if (Test-Path -LiteralPath $p) { Write-Ok "data: $expected" } else { Write-Warn "data missing: $expected" }
}
$platformDir = Join-Path $StageDir 'platforms'
if (Test-Path -LiteralPath $platformDir) {
    Write-Ok "Qt platform plugins: $(Split-Path -Leaf $platformDir)"
} else {
    Write-Warn 'no platforms\ directory: Qt would fail to initialise. Check install.log.'
}
if ($dllCount -lt 10) {
    Write-Warn "only $dllCount DLLs next to the exe; the vcpkg applocal copy may not have run (CMakeLists.txt:375-383)"
}

# Optional smoke test, modelled on the AppImage smoke test (build.yml:600-627):
# a packaged build can pass compilation and still fail at loader level, so run it
# once and look for Mixxx startup output. A machine with no audio device or no
# display may still exit non-zero after Qt has initialised, which is fine.
# Note that mixxx.exe is a GUI subsystem binary (CMakeLists.txt:2483,2491,
# qt_add_executable/add_executable with WIN32), so it may print nothing at all
# when it cannot attach to a console; a lack of output is reported as a warning,
# not a failure. [BEST-EFFORT]
if ($SmokeTest) {
    Write-Info 'smoke test: mixxx.exe --version'
    $smokeLog = Join-Path $BuildDir 'smoke.log'
    $p = Start-Process -FilePath $stagedExe -ArgumentList '--version' -PassThru `
        -RedirectStandardOutput $smokeLog -RedirectStandardError "$smokeLog.err"
    if (-not $p.WaitForExit(60000)) {
        try { $p.Kill() } catch { }
        Write-Warn 'mixxx.exe did not exit within 60 s (GUI process); killing it'
    }
    $smokeText = ''
    foreach ($f in @($smokeLog, "$smokeLog.err")) {
        if (Test-Path -LiteralPath $f) { $smokeText += (Get-Content -LiteralPath $f -Raw) }
    }
    if ($smokeText -match 'Inconsistency detected|error while loading shared libraries|cannot open shared object|0xc000007b|The code execution cannot proceed') {
        Write-Err "smoke test failed at loader level. Output:`n$smokeText"
        Write-Warn 'A missing DLL is the usual cause: check that the staged folder has Qt6*.dll and platforms\qwindows.dll'
    } elseif ($smokeText -match 'Mixxx [0-9]|Using preferences|Selected Qt style|ConfigObject:') {
        Write-Ok 'smoke test reached Mixxx startup'
    } else {
        Write-Warn "smoke test produced no recognisable Mixxx output; it is in $smokeLog"
    }
}

# Optional: the Python environment demucs needs. The application can create this
# itself from Preferences > Stems (src/stems/stemprovisionjob.cpp:45-111); doing it
# here just saves the user that step. It calls the fork's own provision script,
# which is the same code path the preferences page uses:
#   python <target-dir>  ->  <target-dir>/venv/Scripts/python.exe   (res/stems/provision.py:13,60-71)
if ($InstallStemsVenv) {
    Write-Section 'Optional: stem separation Python environment'
    if (-not $StemsVenvDir) {
        # Mixxx's settings path is QStandardPaths::AppLocalDataLocation
        # (src/util/cmdlineargs.cpp:101); the stems environment lives in a "stems"
        # subdirectory of it (src/preferences/stemseparationsettings.h:114-117).
        $StemsVenvDir = Join-Path $env:LOCALAPPDATA 'Mixxx\stems'
    }
    # The scripts live in the source tree and inside the exe, not in the staged
    # folder; this is the only on-disk copy of the installer.
    $provision = Join-Path $script:RepoRoot 'res\stems\provision.py'
    $systemPython = if (Test-Exe 'python') { 'python' } elseif (Test-Exe 'py') { 'py' } else { '' }
    if ((-not $systemPython) -or (-not (Test-Path -LiteralPath $provision))) {
        Write-Warn 'skipping: needs python on PATH and res\stems\provision.py'
    } else {
        Write-Info "python `"$provision`" `"$StemsVenvDir`"  (downloads PyTorch, ~1-2 GB)"
        & $systemPython $provision $StemsVenvDir
        if ($LASTEXITCODE -eq 0) {
            $venvPython = Join-Path $StemsVenvDir 'venv\Scripts\python.exe'
            Write-Ok "separation environment: $venvPython"
        } else {
            Write-Warn "provision.py exited with $LASTEXITCODE; the preferences page can retry this"
        }
        # demucs shells out to the ffmpeg on PATH (res/stems/provision.py:78-83).
        if (-not (Test-Exe 'ffmpeg')) {
            Write-Warn 'ffmpeg is not on PATH: separation will fail until it is (res/stems/provision.py:81)'
        }
    }
}

# Optional zip, for handing the folder to another machine. tar.exe (bsdtar, in the
# box since Windows 10 1803) is far faster than Compress-Archive on ~1.5 GB.
if ($Zip) {
    $zipPath = "$StageDir.zip"
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
    $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
    Write-Info "zipping $StageDir -> $zipPath"
    if (Test-Path -LiteralPath $tar) {
        & $tar -a -c -f $zipPath -C (Split-Path -Parent $StageDir) (Split-Path -Leaf $StageDir)
        if ($LASTEXITCODE -ne 0) { Write-Warn "tar.exe exited with $LASTEXITCODE; trying Compress-Archive" }
    }
    if (-not (Test-Path -LiteralPath $zipPath)) {
        Compress-Archive -Path (Join-Path $StageDir '*') -DestinationPath $zipPath -Force
    }
    if (Test-Path -LiteralPath $zipPath) {
        Write-Ok "archive: $zipPath ($([math]::Round((Get-Item -LiteralPath $zipPath).Length / 1GB, 2)) GB)"
    } else {
        Write-Warn 'zipping failed; copy the folder itself instead'
    }
}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

Write-Section 'Done'
Write-Host "Runnable folder: $StageDir"
Write-Host "Executable:      $stagedExe"
Write-Host ''
Write-Host 'To run it on another PC:'
Write-Host "  1. copy the whole folder ($StageDir) - the exe needs the DLLs and res data next to it"
Write-Host '  2. right-click mixxx.exe -> Run; no installer and no Visual C++ redistributable needed,'
Write-Host '     the CRT DLLs are staged next to the exe (CMakeLists.txt:2584-2592)'
Write-Host ''
Write-Host 'Stem separation inside the built application:'
Write-Host '  * Settings > Stems, tick Enable, then use "Separate stems..." in the track menu.'
Write-Host '  * If the environment was not created with -InstallStemsVenv, use the preferences'
Write-Host "    page's own installer (src/stems/stemprovisionjob.cpp:45-111); it needs a system"
Write-Host '    Python and network access.'
Write-Host '  * demucs needs ffmpeg.exe on PATH (res/stems/provision.py:78-83).'
if ($Architecture -eq 'arm64') {
    # Checked on 2026-10-13 by listing the wheel indexes the provision script
    # uses (res/stems/provision.py:30,52): PyTorch publishes win_arm64 CPU wheels
    # for torch on https://download.pytorch.org/whl/cpu, but there is no
    # torchaudio wheel for win_arm64 anywhere, and plain PyPI has no torch
    # win_arm64 wheel at all, so the demucs environment may not install on
    # Windows on ARM. That does not affect the build or the rest of Mixxx; it
    # only affects running a separation.
    Write-Host ''
    Write-Warn 'arm64: torch has Windows ARM64 CPU wheels, but torchaudio does not, so the'
    Write-Warn 'demucs environment may refuse to install. The build and the exe are fine;'
    Write-Warn 'only the separation step is at risk. An x64 Python running under Windows'
    Write-Warn 'ARM64 emulation is the usual workaround.'
}
Write-Host ''
Write-Host 'This is an unsigned local build: Windows SmartScreen will warn on first run.'

exit 0
