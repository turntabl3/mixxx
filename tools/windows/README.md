# Windows build scripts

`build-mixxx-stems.ps1` builds this fork — Mixxx with the offline stem
separation feature — for Windows **x64** and **arm64** and stages a folder that
can be copied to another PC and run.

```cmd
REM x64 (host architecture is used when the argument is omitted)
powershell -ExecutionPolicy Bypass -File tools\windows\build-mixxx-stems.ps1 -Architecture x64

REM arm64
powershell -ExecutionPolicy Bypass -File tools\windows\build-mixxx-stems.ps1 -Architecture arm64

REM or through the wrapper
tools\windows\build-mixxx-stems.bat x64 -Zip
```

Run `Get-Help .\build-mixxx-stems.ps1 -Full` for all switches. The ones that
matter most: `-BuildEnvBase` (where the prebuilt dependency environment is
unpacked — keep it on a short path), `-StageDir`, `-SkipPrereqInstall`,
`-NoDownload`, `-WithTests`, `-SmokeTest`, `-Zip`, `-InstallStemsVenv`.

## What it does

1. Checks and, where possible, installs the prerequisites: Visual Studio 2022
   Build Tools with the C++ workload, CMake, Ninja, Git, Python. Anything it
   cannot install is reported with exact manual steps and the script exits
   non-zero rather than half-configuring.
2. Downloads, checksum-verifies and unpacks the project's prebuilt dependency
   environment (the same archive CI uses) for the requested architecture. That
   archive contains Qt 6 and every other dependency; building them from source
   would take hours.
3. Configures and builds `mixxx.exe` with RelWithDebInfo, using the same CMake
   options as `.github/workflows/build.yml`, and asserts afterwards that
   `STEM`/`FFMPEG` are on — that is what compiles in `__STEM__`
   (`CMakeLists.txt`, the `if(STEM)` block) — so the staged exe really does
   contain the stem separation feature.
4. Stages a runnable directory through `cmake --install`, which lands the exe,
   the DLLs it needs (vcpkg applocal copy plus the MSVC runtime), the Qt plugins
   and the `res/` data directories flat in one folder, and copies
   `res/stems/*.py` next to the exe as well.

## Verifying what it claims

The script is based on the repository's own Windows CI and packaging scripts;
each command and constant carries a `file:line` reference in the comments.

It has **not been executed on Windows** — no Windows machine was available when
it was written. It was parsed successfully with the PowerShell 7.4 parser
(`[Parser]::ParseFile`, no errors), which catches syntax mistakes but not
runtime behaviour, and every URL and checksum it uses was fetched and verified
from the same sources the script uses.

What was verified about the two published dependency environments (fetched and
unpacked-read on 2026-10-13):

* The SHA-256 values in `tools/windows_buildenv.bat` match both release archives
  exactly, so the script's checksum check will pass rather than block a build.
* Both contain Qt 6 (`share/Qt6/Qt6Config.cmake`, `share/Qt6Core/`,
  `bin/Qt6Widgets.dll`, `bin/Qt6Qml.dll`), FFmpeg, and the libraries behind the
  CI option list (`-DKEYFINDER`, `-DLILV`, `-DHSS1394`, `-DMAD`, `-DMODPLUG`,
  `-DOPUS`, `-DWAVPACK`, `-DQTKEYCHAIN`, `-DBULK`), plus GTest.
* The arm64 environment ships genuine ARM64 binaries (Qt6Core.dll is PE machine
  `0xAA64`), so Qt 6 for arm64-windows is available from this project's
  dependency source.

One caveat that has nothing to do with the build: the demucs environment the
application installs for itself (`res/stems/provision.py`) needs PyTorch. As of
2026-10-13 the PyTorch CPU index publishes `torch` wheels for `win_arm64` but
**no `torchaudio` wheel for `win_arm64`**, and plain PyPI has no `win_arm64`
`torch` wheel at all. On Windows on ARM the separation step may therefore refuse
to install, while the build and the rest of Mixxx are unaffected. An x64 Python
running under Windows ARM64 emulation is the usual workaround.

Expect the usual first-run friction on a real machine: `winget` IDs, the size of
the downloads, and the arm64 link/lib toolset. If a step fails, the script prints
the command, the log path and what to do next.

To build by hand instead, `tools\windows_buildenv.bat` (upstream, unchanged)
plus the commands in `.github/workflows/build.yml` do the same job without the
staging and verification steps.
