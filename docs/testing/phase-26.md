# Phase 26 desktop refinement verification

**Instrumented baseline** `48d155242957a1ea24d68098412944543ab5d276`

**Candidate source** `0442bd91661d575b3e50187212265c88d1e2108a`

**Packaged source** `8516068360fc14151f44b4bbd921d441c0cd7eeb`

## Operator workflows

Fleet now gives aisle progress a distinct heading and uses clearer status colors with text labels. The desktop uses scalable typography and sizes its dashboard columns for the current display scale. Long audit reports put paging controls above the table. Location selection is exercised across Fleet, Location Audits and Robot Maintenance. Maintenance still presents full-brightness LiDAR, motion trails, scan age, localization and hold information.

## Measurement method

The matched comparisons use two Windows Release executables staged with the same app-local DLLs and assets. Each trial uses fresh fictional data, the built-in stockroom and the same seeded sensor profiles. The harness alternates build order. Workloads cover the initial idle Fleet frames, two simultaneous audits, shared-aisle contention, Maintenance and a 200-product audit with 20 products rendered per page.

Frame time measures loop work through presentation and excludes deliberate pacing. The fleet-step measurement includes rendezvous wait. Scripted input measures the work to render and present the frame receiving the injected action, excluding physical device and display latency. Peak working set comes from Windows process counters. Allocation numbers count occupancy-grid backing allocations specifically.

The initial revision-sharing experiment barely reduced grid copies. Reusing storage that no reader holds targets the observed allocation churn even when the map revision changes. Grid immutability tests keep an older snapshot alive while new revisions publish. [Snapshot buffer decision](../decisions/0024-reuse-observed-grid-buffers.md)

```powershell
python tools/profile-desktop.py BASELINE_EXE --candidate CANDIDATE_EXE --pairs 3 --output matched.json
python tools/profile-desktop.py CANDIDATE_EXE --check-large-layout --output large-layout.json
```

## Results

The matched figures are medians across three trials per executable. Times are milliseconds.

| Workload and metric | Baseline p95 / p99 | Candidate p95 / p99 |
| --- | ---: | ---: |
| Fleet frame during the contention run | 0.492 / 0.756 | 0.486 / 0.672 |
| Maintenance frame during parallel auditing | 0.612 / 1.163 | 0.558 / 0.877 |
| Fleet step during parallel auditing | 2.018 / 2.921 | 2.034 / 2.902 |
| Fleet step during contention | 0.144 / 1.092 | 0.140 / 1.092 |
| Snapshot during contention | 0.0080 / 0.0138 | 0.0082 / 0.0144 |
| Scripted input frame during review | 0.568 / 0.568 | 0.471 / 0.471 |
| 200-product audit frame | 0.493 / 0.740 | 0.474 / 1.304 |

The contention run copied 1,177 grids before and 1,176 after. Its backing allocations went from 1,177 to 8 in each candidate trial, a 99.3% reduction. Parallel auditing went from 279 to a median of 7 allocations. Median peak working set during contention was 78.0 MiB in both builds. These counts cover occupancy storage and do not include other allocations.

The 200-product report's p99 rose by 0.56 ms despite a small p95 improvement. The bounded page still renders 20 product rows. The candidate retains above-table paging and scalable text as a usability tradeoff at that measured tail cost. These measurements do not establish total input latency or performance on other hardware. Idle Fleet is included as the startup interval of the contention run. The standalone idle percentile, physical high-DPI monitor transitions, full keyboard-only traversal and hardware cache counters remain unmeasured.

The compact native probes exercised 1280 × 720, including audit reopening, text-edit focus and returning from Maintenance to the same report. The extracted package also rendered 20 products at 1600 × 900 and a simulated 125% font scale without horizontal overflow. Before-and-after 1280 × 720 captures were visually inspected and staged alongside the package. [Operator guide](../operations-center.md#review-discrepancies-and-re-audit-a-location)

## Commands and results

| Command | Result |
| --- | --- |
| `tools/build-windows-package.ps1 -NativeUi` | 136/136 Release tests passed. |
| `tools/test-windows-package.ps1 -NativeUi` | 25/25 package acceptance checks passed. The replacement desktop reported restored audit context and both robots' Maintenance views were exercised. |
| `cmake --workflow --preset windows-debug` | 134/134 passed. |
| `cmake --workflow --preset linux-ci` | 133/133 passed with Clang 18, clang-tidy, ASan, UBSan and leak detection. |
| `cmake --build out/build/windows-phase-11-analysis --target argus-headless argus-desktop --parallel 2` | MSVC `/analyze` passed. |
| Tracked C++ clang-format check, PowerShell and Python parser checks and `git diff --check` | Passed. The CMake target declarations and dependency edges retain the existing acyclic graph. |
| `argus-headless --warehouse small-backroom --sensor-steps 220` | Extracted Phase 25 and Phase 26 archives produced byte-identical stdout with SHA-256 `0abf0712b66b6ec7412dee88698c7c62c49bf616745126d6812803f863a1e7ac`. |

The Windows ZIP, checksum, receipt and captures are staged in `out/release/phase-26`. The archive SHA-256 is `ca416bcee0581fe18fa7d8536f76436a102d8a069ac131e26865f8a450cab489`. Fresh-machine verification remains open.
