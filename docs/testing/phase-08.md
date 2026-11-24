# Phase 8 RFID search and audit verification

**Date** November 24, 2026

**Build and test revision** `0f2b4d6`

## Verification

| Check | Observed result |
| --- | --- |
| `cmake --workflow --preset windows-debug` | 83/83 tests passed with MSVC 19.40.33811. |
| `cmake --workflow --preset windows-release` | 83/83 tests passed. |
| `cmake --workflow --preset linux-ci` | 82/82 tests passed under Ubuntu WSL with Clang 18, clang-tidy, ASan, UBSan and leak detection. |
| `git ls-files -z -- '*.cpp' '*.hpp' '*.hpp.in' \| xargs -0 -r clang-format-18 --dry-run --Werror` | Passed for tracked C++ sources and header templates. |
| MSVC project-code analysis | Inventory and headless executables built with `/analyze`. |
| Dependency and boundary review | Traversal of the CMake link declarations found 16 acyclic project targets. Audit evidence, reporting, and navigation have no simulator or truth references. The simulator adapter uses declared map extents, scenario ID, footprint and seed at assembly. |

The Windows workflows used the local Visual Studio environment wrapper. The Linux workflow used the pinned vcpkg checkout. The MSVC analysis used this separate build.

```bat
cmake --preset windows-debug -B out/build/windows-phase-08-analysis "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /analyze /analyze:external- /external:anglebrackets /external:W0"
cmake --build out/build/windows-phase-08-analysis --target argus-inventory argus-headless --parallel 2
```

## Seeded acceptance run

The `rfid-audit` fixture used seed 5438, a 250 mm grid, and the fictional inventory catalog. A Windows Release run of `argus-inventory DATABASE audit SOFTLINES 811` produced the results below. Linux reproduced the read counts, resolved counts, and duration with session 812. The integration test exercised the complete route, second pass, report and CSV export on both platforms.

| Observation | Value |
| --- | --- |
| Initial and alternate views | Four completed views, each with 14 qualifying frames. |
| Mission duration | 920 ticks, or 46.0 seconds of simulated time. |
| Evidence | 52 raw reads representing three unique EPCs. A subsequent read-only SQLite connection counted the same 52 rows and three distinct EPCs for session 811. |
| Assigned EPCs with a supported aisle | Increased from zero after initial views to two after alternate views. |
| Assignment findings | One clothing EPC supported in SOFTLINES. A second clothing EPC was likely misplaced in TECH. The assigned shoe EPC remained unread after two SOFTLINES views. |
| Expected versus observed | The clothing product had an expected quantity of four and two assigned EPCs. The audit supported one unique clothing EPC in SOFTLINES. The expected quantity remained four. |

The Find Item integration test confirmed the first visit respected the `TECH` last-known hint even though the product's EPCs were assigned to SOFTLINES. Its report contained only the requested product's two assigned EPCs. A repeated session ID was rejected before another investigation started. CSV contained pending review cases with session-and-EPC IDs and reviewer fields. A catalog lookup after the audits still returned both original assignments.

Unit tests exercised same-view deduplication, replay and invalid evidence rejection, cross-aisle ambiguity, independent angle support, and the distinction between no qualifying coverage, one view, and two views without a read. Export tests checked formula prefixes, leading control characters, quotes, commas and JSON control escaping.

## Failures and limits

- The initial MSVC build found an unqualified antenna ID in the simulator adapter. The final code uses the inventory type explicitly.
- The initial Linux static analysis flagged a captured EPC comparison during audit assembly. A bounded set of assigned EPC keys replaced the repeated scan over the assignments.
- The location score is heuristic. The seeded fixture verifies aisle-level discrimination for its configured views. Different layouts, sensor settings, and tag orientations can produce ambiguous or insufficient evidence. Only an independent review can confirm a missing item.
- The CLI uses the two configured aisles in the seeded fixture. Review dispositions live in the exported worksheet. Hosted GitHub Actions is unverified for these commits.
