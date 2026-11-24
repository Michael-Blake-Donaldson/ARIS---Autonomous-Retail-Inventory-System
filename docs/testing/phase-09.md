# Phase 9 mission orchestration verification

**Date** November 24, 2026

**Build and test revision** `9be8363`

## Verification

| Check | Observed result |
| --- | --- |
| `cmake --workflow --preset windows-debug` | 93/93 tests passed with MSVC 19.40.33811. |
| `cmake --workflow --preset windows-release` | 93/93 tests passed. |
| `cmake --workflow --preset linux-ci` | 92/92 tests passed under Ubuntu WSL with Clang 18, clang-tidy, ASan, UBSan and leak detection. |
| `git ls-files -z -- '*.cpp' '*.hpp' '*.hpp.in' \| xargs -0 -r clang-format-18 --dry-run --Werror` | Passed for tracked C++ and header templates. |
| MSVC project-code analysis | The mission, inventory and headless executables built successfully with `/analyze`. |
| Dependency and boundary review | Traversed the CMake link declarations and found 19 acyclic project targets. The state machine and store contain no simulator or world references. The simulator adapter uses configured viewpoints, map extents, footprint and sensor observations. |

The Windows workflows ran through the local Visual Studio wrapper. Linux used the pinned vcpkg checkout and the preset sanitizers. The separate MSVC analysis used the following commands.

```bat
cmake --preset windows-debug -B out/build/windows-phase-09-analysis "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /analyze /analyze:external- /external:anglebrackets /external:W0"
cmake --build out/build/windows-phase-09-analysis --target argus-mission argus-inventory argus-headless --parallel 2
```

## Acceptance evidence

- The transition test enumerated all 130 state/action pairs. Completion required committed scan coverage, and repeated finishes using either the old revision or a terminal revision were rejected.
- Two SQLite connections synchronized a worker with an in-flight scan checkpoint and a concurrent cancellation. Cancellation committed first. The scanner received `stale`, and no stale read count entered the durable checkpoint. Tests also exercised a full queue, identical submissions, changed-intent conflicts, FIFO selection, compatible priority handoff, and priority protection during return.
- The reserve test entered Returning before the simulator reported low battery. A live simulated route charged at the dock and left the interrupted scan paused with zero of four coverage views committed. It retained 425.61 J against a 250 J reserve. The route plan estimated 6,350 mm and wheel odometry measured 6,326 mm over 24.05 simulated seconds.
- A normal simulated scan completed four of four planned views and logged 31 raw reads in 43.55 simulated seconds. Planned travel was 8,700 mm and odometry measured 8,537 mm. The integration test also completed an explicit dock mission, a catalog-backed DPCI search, and an operator-confirmed inspection.
- A separately launched Windows Debug worker was terminated after its first qualified view. The reopened database showed the interrupted state and preserved the committed view, three reads, and 8,050 ms. Recovery changed it to Paused with cause `restart_relocalize`. A Windows Release worker then resumed with a fresh observed pose and finished with four covered views. The portable CLI integration test also confirmed dispatch could not resume work implicitly after an interrupted exit.
- Policy tests exercised obstruction, clearance, stale sensors, lost localization, a data-store fault signal, and timeout. Readiness and revision checks withheld movement in those conditions.

The reserve and normal metrics used the stock `rfid-audit` fixture with separate mission databases. Commands were `argus-mission DATABASE submit 901 scan-all routine` followed by `argus-mission DATABASE run 1600 - reserve` or the same run in `normal` mode.

## Failures and open verification

- The first Clang static-analysis run rejected a mission test helper that copied its input intent. The helper now takes a const reference.
- The mission CLI is a bounded simulator adapter for the configured fixture. The persisted checkpoint carries coverage and aggregate reads. Detailed tag-level observations and human review belong to the inventory audit workflow.
- The reserve is based on grid route distance and an energy estimate. Physical actuation and recovery after real robot localization loss are outside the simulation verification.
- Hosted GitHub Actions is unverified for these local commits.
