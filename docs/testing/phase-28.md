# Phase 28 keyboard audit actions verification

**Code under test** `b53b09ed9e48462c4baa92465d59a10ea34a4fec`

The native probe used SDL keyboard and text events at 1280 × 720. It recovered from a mistyped Waco ID, opened the saved report, and retained the location and selected robot through Maintenance. F10 left the location search active during text entry. F9 focused Mark reviewed without submitting a review. Space then recorded the reviewed timestamp. F10 focused Re-audit location without dispatching. When the probe requested Mark reviewed again after the review had been saved, focus cleared and pressing Space submitted no re-audit. A fresh F10 followed by Space started the re-audit with robot 2 and produced a saved report linked to the reviewed source.

A separate read-only SQLite connection reopened the disposable journal after the probe exited. The Waco had exactly one additional report with the correct source key. The source report gained a review timestamp and retained its original evidence. Its earlier history also matched the input journal. The first full suite exposed an assumption in the probe that the Waco would have one saved report. The fixture already had two. The corrected probe identifies the source and new report by audit identity and works with prior history.

## Commands and results

| Command | Result |
| --- | --- |
| `argus-desktop --ui-fleet-keyboard DISPOSABLE_DATA_DIRECTORY` | Passed against a copy of the journal from the full native workflow. The probe writes to the supplied directory. |
| `cmake --workflow --preset windows-debug` | 134/134 passed. |
| `cmake --build --preset linux-ci --parallel 2` and `ctest --preset linux-ci` | 133/133 passed with Clang sanitizers. This preset does not exercise the native Windows UI. |
| `cmake --build out/build/windows-phase-11-analysis --target argus-headless argus-desktop --parallel 2` | Passed with MSVC `/analyze`. |
| `tools/build-windows-package.ps1 -NativeUi -OutputDirectory out/release/phase-28` | 136/136 Windows Release tests passed. |
| `tools/test-windows-package.ps1 -NativeUi` with the Phase 28 ZIP and SHA-256 | 26/26 extracted-package checks passed, including review and re-audit through the keyboard after replacing the program. |
| `argus-headless --warehouse small-backroom --sensor-steps 220` | Extracted Phase 27 and 28 builds produced identical output with SHA-256 `0abf0712b66b6ec7412dee88698c7c62c49bf616745126d6812803f863a1e7ac`. |
| Tracked changes checked with clang-format 18, PowerShell parser and `git diff --check` | Passed. CMake targets and dependencies are unchanged. |

The Windows ZIP and acceptance receipt are in `out/release/phase-28`. The archive SHA-256 is `432800c75c48efb6814b46bc426ee36d620b06202af75c75feb8de95f91faa0b`.

## Open verification

The host reports one active physical monitor. Windows Sandbox is absent and Hyper-V inventory access was unavailable. Physical DPI transitions and acceptance on a separate clean Windows installation remain open.
