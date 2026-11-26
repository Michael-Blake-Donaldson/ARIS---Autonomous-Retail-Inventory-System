# Phase 25 engineering review verification

**Code under test** `332bffd39f1b6b2c8f3b5270480589537dabcdf5`

The review records subsystem coverage, findings, their disposition and a ranked improvement backlog. [Principal engineering review](phase-25-review.md)

| Gate | Evidence |
| --- | --- |
| Hold during routed work | The new `ObservedRunChecksHoldBeforeSubmittingMotion` test refuses the next motion tick when the cancellation predicate is already false. Sample time stays unchanged. Native hold and berth recovery still preserve completed report evidence. |
| Order overlapping work by acceptance | The desktop traffic probes enqueue `A20J05` and `A20J07` in both robot orders. Both show the later request yielding and then completing. The early hold path removes a reservation that has yet to dispatch. |
| Escape inside an active edit | The native audit review probe activates Location Audits search, injects Escape, observes editing end, checks that the selected report remains open, and continues through review and re-audit. |
| Review handoff | The updated architecture guide describes current ownership and persistence. Outstanding layout, accuracy, fleet profiling, shutdown and clean-host work is ranked in the findings register. |

The cancellation guard acts at the command boundary. Cancellation that arrives once a command is already being submitted can overlap that in-flight tick. The test checks the case where the hold is already pending before submission.

## Commands and results

| Command | Result |
| --- | --- |
| `tools/build-windows-package.ps1 -NativeUi` | 134/134 Release tests passed. |
| `tools/test-windows-package.ps1 -NativeUi` | 25/25 acceptance checks passed. The receipt records both aisle request orders and Escape within the audit search. |
| `cmake --workflow --preset windows-debug` | 132/132 passed. |
| `cmake --workflow --preset linux-ci` | 131/131 passed with Clang 18, clang-tidy, ASan, UBSan and leak detection. |
| `cmake --build --preset linux-tsan --parallel 2` and the routed hold, concurrent mission, shared fleet and aisle access test filters | Nine tests passed with no ThreadSanitizer diagnostics. |
| `cmake --build out/build/windows-phase-11-analysis --target argus-headless argus-desktop --parallel 2` | MSVC `/analyze` passed. |
| Tracked C++ clang-format check, PowerShell syntax check and `git diff --check` | Passed. CMake target declarations and links retain the existing acyclic graph. |
| `argus-headless --warehouse small-backroom --sensor-steps 220` | Extracted Phase 24 and Phase 25 archives produced identical stdout with SHA-256 `0abf0712b66b6ec7412dee88698c7c62c49bf616745126d6812803f863a1e7ac`. |

The package, checksum and acceptance receipt are under `out/release/phase-25`. The Windows ZIP SHA-256 is `4d5d98a9ee38eb8b515045861393db7e4e25c9f39fb767e5aeb487647c7b678d`. Fresh Windows installation verification remains open.
