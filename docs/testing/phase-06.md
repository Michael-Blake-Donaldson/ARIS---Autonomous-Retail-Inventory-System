# Phase 6 navigation and exploration verification

**Date** November 24, 2026

**Build and test revision** `dd73e93`

## Local verification

| Check | Result |
| --- | --- |
| `cmake --workflow --preset windows-debug` | 67/67 tests passed with MSVC 19.40.33811. |
| `cmake --workflow --preset windows-release` | 67/67 tests passed. |
| `cmake --workflow --preset linux-ci` | 66/66 tests passed on Ubuntu under WSL with Clang 18, clang-tidy, ASan, UBSan, and leak detection. |
| `git ls-files -z -- '*.cpp' '*.hpp' '*.hpp.in' \| xargs -0 -r clang-format-18 --dry-run --Werror` | Passed for tracked C++ sources, headers, and header templates. |
| MSVC project code analysis | Desktop, headless, and the navigation evaluator built cleanly with `/analyze`. |
| Dependency review | A depth-first traversal of the CMake link declarations verified 12 acyclic project targets. Navigation links to mapping and transitively to core. Source review found no simulation or world references inside the navigation target. |

The Windows workflows ran through the local MSVC environment wrapper. The Linux workflow used the pinned vcpkg checkout and the preset sanitizers. The separate MSVC analysis configuration used these commands.

```bat
cmake --preset windows-debug -B out/build/windows-phase-06-analysis "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /analyze /analyze:external- /external:anglebrackets /external:W0"
cmake --build out/build/windows-phase-06-analysis --target argus-desktop argus-headless argus-navigation-evaluation --parallel 2
```

## Acceptance evidence

- Fixed-seed reachable coverage passed the defined minimums of 75% for small-backroom and 50% for cluttered-receiving. Compact-stockroom terminated normally at 100% reachable coverage.
- All committed benchmark cases had zero physical contacts and zero interventions from the simulator collision backstop. The compact stockroom completed exploration and the crossing-cart fixture completed a low-battery docking run.
- The crossing-cart fixture recorded six moving-object yields and two replans, then confirmed charging contact. Adding a floor-to-floor no-go strip ended the same mission with `dock_unreachable` and zero translation.
- A* matched an independent uniform-cost solver for all 43 reachable goals in the obstacle fixture. The observed optimality gap was 0%, and the octile estimate did not exceed the reference cost. The tests also cover illegal diagonal corners and no-go inflation.
- Unit tests distinguish exhausted frontiers from unreachable frontiers, enforce a round-trip energy budget, hold the launch point at reserve, and release the deadman when a scan is missing. CLI tests verify the step and dock limits, mutually exclusive modes, and rejection of suffixed option names.

The evaluator used 250 mm cells and the standard seeded encoder and LiDAR profiles. Run `out/build/windows-release/argus-navigation-evaluation.exe` or `out/build/linux-ci/argus-navigation-evaluation` to reproduce the summary.

| Fixture (seed) | Reachable coverage | Simulated time | Contacts / backstop ticks | False obstacle stops / stops | Moving yields | Replans / stuck incidents | End decision |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Small backroom (5489) | 90.68% | 100.00 s | 0 / 0 | 4 / 6 | 4 | 7 / 7 | Step limit, waiting on uncertain pose |
| Cluttered receiving (781) | 65.59% | 27.40 s | 0 / 0 | 1 / 1 | 1 | 1 / 1 | Unsafe estimated cell |
| Broad stockroom (99001) | 36.88% | 100.00 s | 0 / 0 | 6 / 8 | 8 | 3 / 3 | Step limit, waiting on uncertain pose |
| Compact stockroom (507) | 100.00% | 11.80 s | 0 / 0 | 0 / 0 | 0 | 1 / 1 | No frontiers |
| Crossing cart (507) | 100.00% | 22.80 s | 0 / 0 | 1 / 6 | 6 | 2 / 2 | Docked |
| No-go dock (507) | 99.07% | 0.90 s | 0 / 0 | 0 / 0 | 0 | 0 / 0 | Dock unreachable |

Coverage is the fraction of reachable static footprint-safe cells with a map classification. The evaluator flood-fills from the launch cell using cardinal swept-disk edges. Physical contacts are measured at each tick with a 1 mm tolerance for rounding. An obstacle-stop episode is counted as false when the evaluator finds no truth obstacle within a 650 mm forward sweep of the enlarged footprint during the next 12 ticks. A stuck incident is a persistent blockage or waypoint timeout that triggers a replan.

## Failures and open gates

- The first Clang build rejected a partially initialized docking setting under `-Wmissing-field-initializers`. The optional setting now has an explicit empty default.
- The initial crossing-cart scenario failed warehouse schema validation because it supplied only two of the required zone types. The committed fixture declares all five.
- Early test runs exposed intermittent LiDAR clears that let the robot creep into a moving actor's path. The final follower waits for six clear ticks and checks the intermediate cells along its stopping distance. A separate crossing-cart experiment at seed 508 still recorded 16 backstop ticks before `unsafe_position`, with zero physical contacts. Safe recovery from every actor timing remains open.
- Normal frontier exhaustion on the larger built-in layouts remains open. The small and broad fixtures hit the timebox while halted for pose uncertainty, and the cluttered fixture stopped when its estimated current cell became unsafe. Hosted GitHub Actions is unverified for these local commits.
