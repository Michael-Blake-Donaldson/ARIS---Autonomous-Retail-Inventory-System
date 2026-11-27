# Phase 29 desktop display scale transition verification

**Code under test** `6ef0f8c46efae87150d59c60de2cbce5f4307313`

The Windows desktop probe began in a 1280 × 720 window at 100% UI font scale. It queued an SDL display-scale event for 150% while location search had keyboard focus. The edit remained active and Enter opened the saved Waco audit. The probe checked for horizontal overflow and recorded the audit identity and visible product count. Maintenance kept its LiDAR and trail overlays. The probe queued a second SDL event for 100%, returned to Location Audits and verified the selected robot, location, audit identity and product count. The existing keyboard review and linked re-audit then ran to completion.

The first 150% run detected horizontal overflow from long provenance text and the navigation layout. The corrected report wraps the audit IDs, and the navigation pane reserves enough width for its label. The renderer also refreshes its logical font scale when SDL reports a drawable pixel size change for its window. Invalid scale ratios fall back to 100%.

## Commands and results

| Command | Result |
| --- | --- |
| `argus-desktop --ui-fleet-keyboard DISPOSABLE_DATA_DIRECTORY` | The native scale transition, review and re-audit passed. The probe writes to the supplied directory. |
| `cmake --workflow --preset windows-debug` | 134/134 passed. |
| `cmake --build --preset linux-ci --parallel 2` and `ctest --preset linux-ci` | 133/133 passed with Clang sanitizers. This preset exercises the headless paths. |
| `cmake --build out/build/windows-phase-11-analysis --target argus-headless argus-desktop --parallel 2` | Passed with MSVC `/analyze`. |
| `tools/build-windows-package.ps1 -NativeUi -OutputDirectory out/release/phase-29` | 136/136 Windows Release tests passed. |
| `tools/test-windows-package.ps1 -NativeUi` with the Phase 29 ZIP and SHA-256 | 26/26 extracted-package checks passed. The keyboard probe reported `scaleRoundTrip` after package replacement. |
| `argus-headless --warehouse small-backroom --sensor-steps 220` | Extracted Phase 28 and 29 builds produced identical output with SHA-256 `0abf0712b66b6ec7412dee88698c7c62c49bf616745126d6812803f863a1e7ac`. |
| clang-format 18 on changed C++ files, PowerShell parser and `git diff --check` | Passed. CMake target dependencies are unchanged. |

The Windows ZIP and acceptance receipt are in `out/release/phase-29`. The archive SHA-256 is `51e07c335e7c8b2f911a00f377f42cb443143db4e9faca6ef5ee3f9bd24f0723`.

## Open verification

The probe injects effective scales through the SDL event path. This host has one active physical monitor. Windows Sandbox was absent and the session could not access Hyper-V inventory. Physical DPI transitions and acceptance on a separate clean Windows installation remain open.
