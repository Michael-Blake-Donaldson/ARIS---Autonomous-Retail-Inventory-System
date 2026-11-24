# ARGUS

ARGUS is an offline C++20 simulator for autonomous retail stockroom mapping and RFID inventory audits. Drive the simulated robot in the Windows 3D operations center or run autonomous exploration from the command line. The headless executable can export and reload estimated maps.

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
out\build\windows-debug\argus-headless.exe --warehouse small-backroom --drive-steps 220
out\build\windows-debug\argus-headless.exe --warehouse small-backroom --sensor-steps 36
out\build\windows-debug\argus-headless.exe --warehouse small-backroom --map-steps 80
out\build\windows-debug\argus-headless.exe --warehouse small-backroom --explore-steps 2000
out\build\windows-debug\argus-headless.exe --warehouse small-backroom --export-map 80 > backroom.argmap
out\build\windows-debug\argus-headless.exe --warehouse small-backroom --map-stdin < backroom.argmap
out\build\windows-debug\argus-headless.exe --export-warehouse small-backroom > scenario.json
out\build\windows-debug\argus-headless.exe --scenario-stdin --tick 40 < scenario.json
```

These examples use Command Prompt redirection. On Linux, use `out/build/linux-ci/argus-headless` with the same flags. The built-in warehouse names are `small-backroom`, `cluttered-receiving`, `broad-stockroom`, and `rfid-audit`. Omitting `--tick` starts at zero. Headless warehouse runs emit one JSON snapshot to stdout. An explicit fixture export includes the authored fictional placements.

The desktop requires an interactive Windows session with OpenGL support. It defaults to `small-backroom` and advances the simulator every 50 ms. Hold Space and use the arrow keys to drive, or select Explore for autonomous navigation. Release Space to brake, press X for a latched emergency stop, and press R with Space released to clear it. F4 toggles LiDAR, F5 opens the estimated occupancy view, and F3 opens Diagnostics. See the [operations center guide](docs/operations-center.md) for the cameras, inventory views, mission workbench, and recording commands.

Use `--drive-steps N` with a warehouse to run 1–2,000 straight-drive steps. Headless output includes odometry, commanded and measured velocities, battery and collision status. In the desktop, the same flag runs the probe before manual control begins.

Use `--sensor-steps N` to capture wheel odometry, LiDAR ranges and fictional RFID reads during the drive. The output includes timestamps, estimated poses, and the queue's produced, consumed and dropped counts.

Use `--explore-steps N` for a bounded autonomous survey with route planning and obstacle yielding. Add `--dock-mm X Y` to configure a charging waypoint in the map frame. The probe reports its exit reason, map classification counts, and replanning activity. See the [navigation interface](docs/navigation.md) and [Phase 6 verification](docs/testing/phase-06.md).

Use `argus-inventory` to import the fictional CSV or JSON catalog into SQLite and look up assigned EPCs by DPCI. The repository also stores RFID reads by mission and scan session. See the [inventory commands and formats](docs/inventory.md).

Use `argus-inventory DATABASE find-item DPCI SESSION` to investigate likely aisles and `argus-inventory DATABASE audit AISLE SESSION` to compare expected stock with unique observed EPCs. See the [RFID audit and review workflow](docs/audit.md).

Use `argus-mission` to queue scan, search, charging, and inspection work. The SQLite journal preserves checkpoints and records preemption, cancellation, and recovery. See the [mission commands and recovery policy](docs/missions.md).

The original headless RNG probe remains available as `--seed UINT32`. Seed 5489 produces `{"seed":5489,"firstSample":3499211612}`. Both executables accept standalone `--help` and `--version`, and write structured startup and error events to stderr. Exit codes are 0 on success, 2 for invalid options, and 1 for an import or runtime failure.

See the [mapping and snapshot format](docs/mapping.md), [Phase 5 evidence](docs/testing/phase-05.md), [sensor models](docs/sensors.md), and [robot controls](docs/robot.md).
