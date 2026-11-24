# Phase 7 inventory persistence verification

**Date** November 24, 2026

**Build and test revision** `c94deb9`

## Verification

| Check | Observed result |
| --- | --- |
| `cmake --workflow --preset windows-debug` | 78/78 tests passed with MSVC 19.40.33811. |
| `cmake --workflow --preset windows-release` | 78/78 tests passed. |
| `cmake --workflow --preset linux-ci` | 77/77 tests passed in Ubuntu under WSL with Clang 18, clang-tidy, ASan, UBSan, and leak detection. |
| `git ls-files -z -- '*.cpp' '*.hpp' '*.hpp.in' \| xargs -0 -r clang-format-18 --dry-run --Werror` | Passed for all tracked C++ files. |
| MSVC project-code analysis | The inventory CLI and its linked project libraries built successfully with `/analyze`. |
| Dependency review | A depth-first traversal of CMake link declarations found 14 acyclic project targets. Inventory links to core, SQLite, and the JSON parser. Inventory source contains no references to simulation/world headers or truth interfaces. |

The Windows workflows ran through the local Visual Studio environment wrapper. Both platforms built the manifest-pinned SQLite 3.53.4 port. The MSVC analysis ran with a separate output directory.

```bat
cmake --preset windows-debug -B out/build/windows-phase-07-analysis "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /analyze /analyze:external- /external:anglebrackets /external:W0"
cmake --build out/build/windows-phase-07-analysis --target argus-inventory --parallel 2
```

## Acceptance evidence

- The CLI imported four fictional product categories from CSV and JSON into separate databases. Each lookup ran in a new process. A DPCI lookup for `990-01-0001` returned both assigned EPC records. A subsequent lookup using the first EPC cursor returned only the second record. A repository test verified the 256-record page boundary.
- Product CRUD and referential integrity passed. Product names containing SQL-like punctuation survived restart unchanged. Malformed lookup IDs were rejected and foreign keys prevented deletion of a product with dependents.
- Duplicate EPC and dangling bin tests verified rollback of product rows already executed in the transaction. An early timestamp rolled back an evidence batch. A separate connection held the write lock until the repository returned `busy`. The same batch then succeeded after the lock was released.
- A version 1 database with existing catalog records migrated to version 2 and accepted scan sessions. A deliberately conflicting migration left the previous version and catalog intact. Foreign and newer-version databases were rejected. Migration recovery is documented as a restore from backup.
- The bounded parser rejected the malformed CSV and JSON corpus, every incomplete prefix of the UTF-8 JSON fixture, and inputs above file and record limits. An additional 512 deterministic byte mutations exercised both formats.
- Import tests rejected paths containing traversal, absolute paths, Windows drive and alternate-stream syntax, reserved device names, directory names, hard links, and oversized files. The Linux run also rejected an actual external symbolic link and a FIFO without blocking.
- An RFID capture from the seeded simulator was saved via the public observation adapter. A separate evidence round-trip verified persistence of session, EPC, timestamp, estimated pose, antenna, confidence, RSSI and opaque raw bytes. An unknown EPC remained discoverable as evidence while expected quantity and assigned EPCs stayed unchanged.

## Limits and open verification

- This Windows account could not create a symbolic link, so the Windows symbolic-link-specific test branch was skipped. The hard-link and other Windows path checks ran. Linux exercised the symbolic-link branch.
- The inventory CLI exposes catalog import and lookup. Scan writes and reads are available through the repository API. The simulator emits signal observations without location confidence, so the adapter stores zero location confidence and an empty raw packet.
- Hosted GitHub Actions is unverified for these local commits.
