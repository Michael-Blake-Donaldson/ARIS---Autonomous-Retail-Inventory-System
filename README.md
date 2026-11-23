# ARGUS

ARGUS is an offline C++20 simulator for autonomous retail stockroom mapping and RFID inventory audits. The Windows desktop previews warehouse layouts and moving obstacles. The headless executable can validate, export and replay the same scenarios.

## Build and test

Install Git, CMake 3.25 or later, and Ninja. On Windows, install Visual Studio 2022 with the Desktop development with C++ workload and a Windows SDK. Run the following from an x64 Native Tools Command Prompt at the repository root.

```bat
git clone --branch 2026.10.16 --single-branch https://github.com/microsoft/vcpkg.git "%LOCALAPPDATA%\argus-vcpkg"
set "VCPKG_ROOT=%LOCALAPPDATA%\argus-vcpkg"
git -C "%VCPKG_ROOT%" rev-parse HEAD
```

Confirm the printed revision matches `builtin-baseline` in [vcpkg.json](vcpkg.json). Then bootstrap the tool and run both workflows.

```bat
call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
set "VCPKG_DISABLE_METRICS=1"
set "VCPKG_MAX_CONCURRENCY=4"
cmake --workflow --preset windows-debug
cmake --workflow --preset windows-release
```

If `cmake` or `ninja` is missing from the prompt, add their installation directories to that prompt's PATH. vcpkg downloads dependency sources and may download its own build tools during the first configure.

On Ubuntu 24.04 x64, use the Linux verification preset for the headless targets. On WSL, prefer a checkout on the Linux filesystem.

```sh
sudo apt-get update
sudo apt-get install -y git build-essential cmake ninja-build curl zip unzip tar pkg-config clang-18 clang-tidy-18 clang-format-18 libclang-rt-18-dev
git clone --branch 2026.10.16 --single-branch https://github.com/microsoft/vcpkg.git "$HOME/argus-vcpkg"
export VCPKG_ROOT="$HOME/argus-vcpkg"
git -C "$VCPKG_ROOT" rev-parse HEAD
# Confirm this is the manifest baseline before bootstrapping.
"$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
export VCPKG_DISABLE_METRICS=1
git ls-files -z -- '*.cpp' '*.hpp' '*.hpp.in' | xargs -0 -r clang-format-18 --dry-run --Werror
cmake --workflow --preset linux-ci
```

## Run

```bat
out\build\windows-debug\argus-desktop.exe --warehouse cluttered-receiving
out\build\windows-debug\argus-headless.exe --warehouse broad-stockroom --tick 40
out\build\windows-debug\argus-headless.exe --export-warehouse small-backroom > scenario.json
out\build\windows-debug\argus-headless.exe --scenario-stdin --tick 40 < scenario.json
```

These examples use Command Prompt redirection. On Linux, use `out/build/linux-ci/argus-headless` with the same flags. The built-in warehouse names are `small-backroom`, `cluttered-receiving`, and `broad-stockroom`. Omitting `--tick` starts at zero. Headless warehouse runs emit one JSON snapshot to stdout. An explicit fixture export includes the authored fictional placements.

The desktop window requires an interactive Windows session with OpenGL support. It defaults to `small-backroom` and advances actor motion at 50 ms per tick. Close it with Escape or the window close button. The preview shows the floor plan, moving obstacles and the stationary robot spawn marker.

The original headless RNG probe remains available as `--seed UINT32`. Seed 5489 produces `{"seed":5489,"firstSample":3499211612}`. Both executables accept standalone `--help` and `--version`, and write structured startup and error events to stderr. Exit codes are 0 on success, 2 for invalid options, and 1 for an import or runtime failure.

See the [warehouse format and interfaces](docs/warehouse.md), [Phase 2 evidence](docs/testing/phase-02.md), and [Phase 1 evidence](docs/testing/phase-01.md).
