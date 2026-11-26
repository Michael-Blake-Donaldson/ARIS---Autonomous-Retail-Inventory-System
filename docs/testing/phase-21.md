# Phase 21 discrepancy review and re-audit verification

**Date** November 26, 2026

**Code under test** `f7a9b92aec68043870805b17f1d3880aaa5e1e9c`

## Acceptance

| Gate | Evidence |
| --- | --- |
| Review filter and evidence | The 1280 × 720 desktop probe clicked **Needs review only** and **Mark reviewed** for `A10J05`. It checked that the filter and A10 count each lost one pending location and that the original report bytes stayed identical. |
| Re-audit and comparison | The native probe clicked **Re-audit location**, waited for a fresh completed report, and clicked **Compare with source audit**. It verified the link, the comparison table and the return of the outstanding discrepancy to review. The comparison unit test covers shared, removed and added DPCIs. |
| Persistence and migration | A fresh desktop process rendered the same comparison and loaded both the reviewed source and the new unresolved review state. A copy of a Phase 20 journal migrated from schema 4 to 5 and kept the original whole-shelf and Waco payload bytes, timestamps and identities. |
| Stale and invalid inputs | Unit tests reject a source from another location, a superseded source, self-links and stale acknowledgments. Retrying identical writes is idempotent. Retention keeps the 20-report limit and preserves the source identity on a linked report after the source ages out. |
| Existing behavior | Native tests also cover the original operations tour, Maintenance for both robots, fleet yielding and restart. The seeded headless sensor replay matched Phase 20 byte for byte. |

## Commands and results

| Command | Result |
| --- | --- |
| `cmake --build --preset windows-release --parallel 2` and `ctest --preset windows-release --output-on-failure` | 129/129 passed, with native tests enabled. |
| `cmake --workflow --preset windows-debug` | 127/127 passed. |
| `cmake --workflow --preset linux-ci` | 126/126 passed with Clang 18, clang-tidy, ASan, UBSan and leak detection. |
| `cmake --build out/build/windows-phase-11-analysis --target argus-headless argus-desktop --parallel 2` | MSVC `/analyze` passed. |
| `git ls-files -z -- '*.cpp' '*.hpp' '*.hpp.in' \| xargs -0 -r clang-format-18 --dry-run --Werror` and `git diff --check` | Passed. The declared CMake link graph is acyclic, including the optional fuzz targets. |
| `argus-headless --warehouse small-backroom --sensor-steps 220` | Phase 20 and Phase 21 stdout shared SHA-256 `0abf0712b66b6ec7412dee88698c7c62c49bf616745126d6812803f863a1e7ac`. |

## Reproduce

```powershell
$data = New-Item -ItemType Directory -Path (Join-Path 'out/evidence' ('review-' + [Guid]::NewGuid()))
out/build/windows-release/argus-desktop.exe --ui-fleet-smoke $data.FullName
out/build/windows-release/argus-desktop.exe --ui-fleet-review $data.FullName
out/build/windows-release/argus-desktop.exe --ui-fleet-review-reopen $data.FullName
```

Open **Location Audits** with the same operations directory to check the reviewed source under **Audit history** and the outstanding findings on the new report. The review timestamp records a local acknowledgment. Completed reviews and re-audit links persist when an operations directory is supplied. [Operator workflow](../operations-center.md#review-discrepancies-and-re-audit-a-location)

## Local release

The unsigned Windows x64 ZIP in `out/release/phase-21` has SHA-256 `1c5413f03458d0467b95ee348bad8604c300ffd3a68e94c068a73bac0817f52e`. The package build passed 129 tests. The ZIP passed 17 standalone checks and the acceptance receipt records its source and digest. The extracted program also ran the fleet integration script with a system-only PATH.

Verification used the local Windows host and WSL. Fresh-machine verification remains open. Rollback to Phase 20 requires a copy of the journal from before the schema 5 migration.
