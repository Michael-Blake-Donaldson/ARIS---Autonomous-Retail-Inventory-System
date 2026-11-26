# Phase 18 operator dashboard verification

**Date** November 26, 2026

**Packaged source** `0f4984964f18a5287f7a7085f73b59c0dd067992`

## Acceptance

| Gate | Evidence |
| --- | --- |
| One Fleet map | Fleet draws the authored stockroom, both observed robot positions and the selected route in one viewport. The native probe clicks zoom and verifies that the shelf ID is visible and selectable. |
| Two clicks to a saved audit | The native probe clicks the `A20J05` map label, then **Open audit**. The product table renders all rows from the stored report. |
| Consistent selection and visible hold | The probe opens Robot Maintenance with Robot 2 still selected and checks the original LiDAR and trail overlays. It interrupts a restarted shelf audit and checks that the reported operator hold appears in Fleet. |
| Compact layout | The native reopen probe runs at 1280 × 720 and checks that the visited dashboard panes have no horizontal scroll range. |
| Native and headless compatibility | The existing operations tour and new fleet reopening probe pass in the same release build. The project link graph remains acyclic across 22 declared targets. |

The Location Audits sidebar accepts a case-insensitive shelf ID search. It shows completion and discrepancy statuses independently. The product table shows rows in DPCI order with a page size of 20. RFID tags, previous audits and maintenance counters expand on demand. The fleet map uses the two shelf locations in the authored demo fixture.

## Reproduce

Run from the repository root in an interactive Windows session. Use a fresh disposable data directory.

```powershell
$root = New-Item -ItemType Directory -Force 'out/evidence/phase-18'
$data = New-Item -ItemType Directory -Path (Join-Path $root.FullName ('dashboard-' + [Guid]::NewGuid()))
out/build/windows-release/argus-desktop.exe --ui-fleet-smoke $data.FullName
out/build/windows-release/argus-desktop.exe --ui-fleet-reopen $data.FullName
```

The first command creates both shelf reports. The second runs the native dashboard interaction test in a fresh process. With `ARGUS_ENABLE_NATIVE_UI_TESTS=ON`, the CTest entry `argus-desktop.fleet` runs both commands. To inspect the views directly, start the application with `--operations-data` and the same directory.

The native frame stream for the review can be recorded with `--ui-dashboard-record DIRECTORY` and the existing binary-safe encoding recipe. The packaged capture decoded 115 H.264 frames at 1280 × 720 and 20 fps for 5.75 seconds. Visual review checked the aisle and shelf labels, the selected robot, the product title and quantity columns, and the maintenance scene. The local release includes `ARGUS-phase-18-dashboard.mp4` and `ARGUS-phase-18-fleet.png`. [Operations center guide](../operations-center.md#local-verification-and-recording)

## Quality gates

| Command | Result |
| --- | --- |
| `tools/build-windows-package.ps1 -NativeUi` | Windows Release passed all 117 tests, including the two native tours. |
| `tools/test-windows-package.ps1 -NativeUi` | All 17 standalone portable checks passed against the final ZIP. The two-process fleet check also ran from its extraction with a system-only PATH. |
| `cmake --workflow --preset windows-debug` | 115/115 passed. |
| `cmake --workflow --preset linux-ci` | 114/114 tests passed with Clang 18, clang-tidy, ASan, UBSan and leak detection. |
| `cmake --build out/build/windows-phase-11-analysis --target argus-headless argus-desktop --parallel 2` | Passed MSVC `/analyze` after replacing two runtime array accesses with checked lookups. |
| `git ls-files -z -- '*.cpp' '*.hpp' '*.hpp.in' \| xargs -0 -r clang-format-18 --dry-run --Werror` and `git diff --check` | Passed. |

The final local ZIP under `out/release/phase-18-reviewed` has SHA-256 `ce08ed8c345d69c6ea65bcebbf44adbad08b2742423b6a44760c64e6787f4a66`. The verification host is Windows 11 Pro x64 build 26200. Fresh-machine verification remains open.

Preliminary standalone acceptance runs hit a Windows file lock during cleanup. The test harness now retries I/O failures for up to 12 attempts within the validated temporary workspace. A separate packaged fleet attempt held both queued audits before dispatch. A fresh run after the build and checks finished completed the native workflow.
