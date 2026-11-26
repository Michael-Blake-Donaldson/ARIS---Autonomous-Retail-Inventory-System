# Phase 23 fleet failure recovery verification

**Code under test** `5f558bcb297cc8568f1375de0df1d1f514d1cda0`

## Acceptance

| Gate | Evidence |
| --- | --- |
| Evidence survives an interrupted return | The native reopening probe runs a completed Waco re-audit and interrupts the robot while it is returning. It verifies the retained aisle claim, a new report linked to the original, and matching report bytes from a fresh journal connection. |
| A stalled peer cannot advance the fleet | The fleet test commits one shared tick and then withholds the second robot's next submission. The first call returns after the test deadline. A late submission is rejected, both world and sample ticks stay at one, and explicit closing preserves the timeout reason. |
| Ordinary recovery and traffic still work | The native probe holds a robot before its survey completes and uses **Return to berth** to release the aisle. The separate `A20J05` and `A20J07` probe verifies yielding and completion. |

## Commands and results

| Command | Result |
| --- | --- |
| `ctest --preset windows-release -V -R '^(SharedFleet\|argus-desktop.fleet)'` | 5/5 passed during initial integration. |
| `tools/build-windows-package.ps1 -NativeUi` | 130/130 Windows Release tests passed. |
| `tools/test-windows-package.ps1 -NativeUi` | 22/22 passed from the extraction. The receipt includes `auditSavedAfterHold` from the replacement program. |
| `cmake --workflow --preset windows-debug` | 128/128 passed. |
| `cmake --workflow --preset linux-ci` | 127/127 passed with Clang 18, clang-tidy, ASan, UBSan and leak detection. |
| `cmake --build --preset linux-tsan --parallel 2` and `TSAN_OPTIONS=halt_on_error=1 out/build/linux-tsan/argus_unit_tests --gtest_filter="SharedFleet.*:AisleAccess.*" --gtest_repeat=3` | All six fleet and reservation tests passed in three repetitions with zero ThreadSanitizer diagnostics. |
| `cmake --build out/build/windows-phase-11-analysis --target argus-headless argus-desktop --parallel 2` | MSVC `/analyze` passed. |
| Full tracked C++ clang-format check and `git diff ea08d8d --check` | Passed. Project target declarations and dependency edges are unchanged. |
| `argus-headless --warehouse small-backroom --sensor-steps 220` | Phase 22 and Phase 23 archives produced byte-identical stdout with SHA-256 `0abf0712b66b6ec7412dee88698c7c62c49bf616745126d6812803f863a1e7ac`. |

The five-second deadline bounds the peer's rendezvous wait and preserves the last committed joint state. Restart is required after a clock fault. A worker stalled outside the rendezvous in a system call may still delay process exit. The native interruption probe exercises the durable journal, while the missing-peer regression exercises the clock directly. Fresh Windows installation verification remains open.

The Windows archive in `out/release/phase-23` has SHA-256 `e8590d657b9e8b7efe726d20e59432db77efd9b5a827d4593127613aad22dda6`.
