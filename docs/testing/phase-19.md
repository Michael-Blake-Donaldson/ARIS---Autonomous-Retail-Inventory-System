# Phase 19 mixed backroom location verification

**Date** November 26, 2026

**Code under test** `f60f4c6b825d59d74cd2de76b598589ba5c108a0`

## Acceptance

| Gate | Evidence |
| --- | --- |
| Auditable hierarchy | Four aisles expose three shelf levels each. A20J06 expands to ten Wacos. The fixture has 21 auditable leaves. A20 contains 12 leaves because the divided shelf contributes only its ten Wacos. |
| Operator dashboard | Fleet shows aisle completion and review totals. The native reopening probe opens the Waco report from Fleet, preserves robot selection through Maintenance, and verifies a hold and a 1280 × 720 layout without horizontal scrolling. |
| Independent location work | Two native workers ran concurrently. Robot 1 completed A10J05 and Robot 2 completed A20J06-W01. A20J06-W02 stayed pending. A separate observed-navigation test moved Robot 2 to A40J07 and verified a focused return from the top level. |
| Evidence quality | The whole-shelf audit confirmed one tee and retained the expected shoe with zero reads. The Waco audit retained one unresolved shoe EPC, with zero confirmed reads. Unit tests verify that two separated confined views can confirm a shared tag and that single-view, overlapping and repeated-view evidence remains unresolved. |
| Persistence and compatibility | A fresh desktop process reopened both completed location reports. A copy of a Phase 18 database migrated from version 3 to 4 and retained the A10J05 and A20J05 report bytes and provenance. The default headless sensor replay matched Phase 18 byte for byte. |
| Maintenance | The scripted tour observed fresh LiDAR and a motion trail during translation for both robots. Fleet maintenance uses the expanded stockroom scene. The original single-robot operations tour also passes. |

## Commands and results

| Command | Result |
| --- | --- |
| `cmake --build --preset windows-release --parallel 2` and `ctest --preset windows-release` with native tests enabled | 121/121 passed. |
| `cmake --build --preset windows-debug --parallel 2` and `ctest --preset windows-debug` | 119/119 passed. |
| `cmake --build --preset linux-ci --parallel 2` and `ctest --preset linux-ci` | 118/118 passed with Clang 18, clang-tidy, ASan, UBSan and leak detection. |
| `tools/build-windows-package.ps1 -NativeUi` and `tools/test-windows-package.ps1 -NativeUi` | Packaged Windows Release passed 121/121. The ZIP passed 17 standalone checks. The extracted executable also passed both fleet probes with a system-only PATH. |
| `cmake --build out/build/windows-phase-11-analysis --target argus-headless argus-desktop --parallel 2` | Passed MSVC `/analyze`. |
| `git ls-files -z -- '*.cpp' '*.hpp' '*.hpp.in' \| xargs -0 -r clang-format-18 --dry-run --Werror` and `git diff --check` | Passed. Declared CMake links are acyclic across 22 project targets. |
| `argus-headless --warehouse small-backroom --sensor-steps 220` for Phase 18 and Phase 19 | Byte-identical output with SHA-256 `0abf0712b66b6ec7412dee88698c7c62c49bf616745126d6812803f863a1e7ac`. |

The early tests surfaced sparse returns on the expanded floor and horizontal scroll when the Waco tree opened. The fleet-only LiDAR profile now reaches the enclosure at 30 m with 360 beams. Cross-aisle routing uses bounded waypoints and the existing observed planner. The Location Audits sidebar reserves the vertical scrollbar and owns its scroll state. The final native test exercised both corrections. A Linux link started during iterative source changes used an older library object. The final incremental build regenerated it.

## Reproduce

```powershell
$root = New-Item -ItemType Directory -Force out/evidence/phase-19
$data = New-Item -ItemType Directory -Path (Join-Path $root.FullName ('mixed-' + [Guid]::NewGuid()))
out/build/windows-release/argus-desktop.exe --ui-fleet-smoke $data.FullName
out/build/windows-release/argus-desktop.exe --ui-fleet-reopen $data.FullName
out/build/windows-release/argus-desktop.exe --operations-data $data.FullName
```

Open A20 in **Location Audits** to inspect the divided J06 shelf and its Wacos. Reports, statuses and unresolved reads refer to individual locations. [Operations center guide](../operations-center.md#run-a-two-robot-shelf-audit)

## Local release

The ZIP in `out/release/phase-19` has SHA-256 `ea683ce1e56921aa5ad1635398fa94f929b597b2c704d7f309f90ab3e20d921f`. The acceptance receipt matches that digest and the packaged source. The same extracted executable captured `ARGUS-phase-19-stockroom.mp4`. FFprobe decoded 115 H.264 frames at 1280 × 720 and 20 fps. Visual review checked Fleet's four aisle rollups, the expanded J06 Waco list, the product details with an unresolved RFID section, and the expanded Maintenance scene.

The focused RFID head, the 0.02 mm simulated encoder profile, and the separate robot worlds are simulation choices. The verified Waco audit illustrates conservative review of a genuine read when the estimated footprint could not isolate its source. The test host is Windows 11 Pro x64. Fresh-machine verification remains open.
