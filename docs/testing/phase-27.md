# Phase 27 keyboard and idle Fleet verification

**Code under test** `b2eebad3193d37ae3f846c7a68354ca883309466`

## Keyboard workflow

The native probe started in Fleet with saved reports and used only keyboard and text events. Ctrl+F focused location search. Entering `A20J06-W00` produced a no-match state without replacing the selection with the target report. The probe reopened search, entered ` a20j06-w01 ` and opened the Waco report with Enter. F8 opened Maintenance with LiDAR and trail layers enabled. F7 restored the same robot and populated report, and F6 returned to Fleet. [Keyboard guide](../operations-center.md#review-discrepancies-and-re-audit-a-location)

This verifies finding and reading a saved report, including recovery from a typo. Keyboard-only review submission and re-audit dispatch remain unverified.

## Idle measurement

Run the profiler on the extracted desktop. Each run uses a fresh disposable data directory and the same built-in, seeded scenario. Fleet holds one view for 2.6 seconds without submitting work. Frame samples exclude the first 400 ms. Worker timing and allocation counts cover the full run, including startup. The harness requires at least 90 rendered frame samples per trial. Fleet-step timing includes the rendezvous.

```powershell
python tools/profile-desktop.py PATH_TO_ARGUS_DESKTOP --idle-only --pairs 3 --output idle.json
```

| Trial | Fleet frames | Frame p95 / p99 (ms) | Fleet step p95 / p99 (ms) | Grid allocations | Peak working set |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 175 | 0.564 / 0.891 | 0.052 / 1.036 | 6 | 78.45 MiB |
| 2 | 175 | 0.396 / 0.528 | 0.048 / 0.135 | 6 | 77.68 MiB |
| 3 | 174 | 0.434 / 0.752 | 0.051 / 0.306 | 6 | 77.73 MiB |

The measured median frame p95 and p99 were 0.434 ms and 0.752 ms. Frame timing includes presentation and excludes deliberate pacing. The results are a local idle baseline on the extracted Release package.

## Commands and results

| Command | Result |
| --- | --- |
| `tools/build-windows-package.ps1 -NativeUi` | 136/136 Windows Release tests passed, including the keyboard probe. |
| `tools/test-windows-package.ps1 -NativeUi` | 26/26 checks passed, including the keyboard workflow after replacing the program. |
| `cmake --workflow --preset windows-debug` | 134/134 passed. |
| `cmake --build --preset linux-ci --parallel 2` and `ctest --preset linux-ci` | 133/133 passed with the existing Clang sanitizer build. |
| `cmake --build out/build/windows-phase-11-analysis --target argus-headless argus-desktop --parallel 2` | MSVC `/analyze` passed. |
| Tracked C++ clang-format, PowerShell and Python syntax, and `git diff --check` | Passed. CMake target declarations and links are unchanged. |
| `argus-headless --warehouse small-backroom --sensor-steps 220` | The extracted Phase 26 and 27 builds produced identical output with SHA-256 `0abf0712b66b6ec7412dee88698c7c62c49bf616745126d6812803f863a1e7ac`. |

The ZIP, acceptance receipt and idle profile are staged in `out/release/phase-27`. The archive SHA-256 is `4e2a3ddf901bee9b81698de70f85186d7145f2277afdefedf5f66abfe18fa4a0`.

## Open verification

The local host reports one active physical monitor record. Windows Sandbox is unavailable and the current account cannot access the Hyper-V inventory. The separate clean Windows installation and physical DPI-transition checks remain open.
