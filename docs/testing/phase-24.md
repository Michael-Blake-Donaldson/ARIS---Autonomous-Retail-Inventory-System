# Phase 24 fault handling and recovery verification

**Code under test** `da7f198488f2c8104424cb6c1a494b40cd3ea881`

All destructive inputs were created in disposable test directories. The product and location IDs are fictional.

## Fault matrix

| Trigger | Expected feedback and recovery | Verification |
| --- | --- | --- |
| Blank or mistyped DPCI or location | Explain the expected DPCI format or show a correctable no-match state. Accept a corrected search and tolerate surrounding spaces and tabs. Clearing a location search preserves the current report selection. | Search model regression checks blank input, mistakes and corrected IDs. Desktop renders the hints and clear action. |
| Repeated audit request or a second robot requesting an active location | Reject duplicate dispatch. The other robot can proceed on a different location. | The native fleet smoke tries both duplicates before completing legitimate concurrent audits. |
| Stale audit or review identity | Reject the action without changing the current report. Reopen ARGUS to obtain current history. | Journal source and stale review unit tests. |
| A file supplied as the operations directory | Exit with status 2 and explain that an existing folder is required. Correct the argument and launch. | Desktop CLI test and extracted-package acceptance. |
| Malformed, oversized, incompatible or truncated import | Reject before changing the active data. Accept a valid input through the normal entry point. | Scenario, catalog and map parser tests and CLI fixtures. The portable check includes map replay and corruption rejection. |
| Damaged fleet journal | Warn in Fleet, pause audit and review writes and preserve the journal. Simulation remains available. | Native disposable damaged-journal probe checks visible feedback, rejection of new audit work, continued simulation ticks and byte equality of the file. The same case runs from the extracted package. |
| Old journal format | Migrate atomically and reopen existing audit evidence. | Mission and inventory migration fixtures and later native reopen. |
| Another writer holds the journal | Preserve the existing report and its prior review status. Retry after the lock clears. | A separate SQLite connection holds a write lock. The attempted review returns busy. After release, the retry records one timestamp. |
| SQLite capacity exhausted during a re-audit | Roll back the attempted report and preserve its source. Retry once sufficient capacity is available. | The journal test imposes a small quota, triggers `SQLITE_FULL`, opens a fresh connection and confirms only the original report exists. A smaller retry links correctly and a duplicate retry leaves two reports total. |
| Denied write path or read-only journal | Reject the write. Retrying after permission is restored succeeds. | CLI import-path tests and the extracted package's denied-write case. A read-only attribute on a disposable journal caused the packaged mission CLI to return `read_only`. Its SHA-256 stayed identical. Removing the attribute allowed the same command to queue the mission. |
| A writer dies before committing | Reopen the database, preserve reports byte for byte, and accept valid work. | A disposable external SQLite process was terminated during an uncommitted report update with a rollback journal present. The packaged mission CLI then queued new work and all prior shelf records matched. |
| Worker stalls, LiDAR ages or a route blocks | Stop or yield as appropriate, and hold the last committed state. Resume when observations recover or restart when the fleet clock closes. | Shared clock, aisle access and navigation tests. The native operations tour verifies LiDAR dropout and recovery, and the traffic test verifies yielding and completion. |
| Focus loss while a robot returns and holds an aisle claim | Apply the hold when the event arrives. Retain the claim and reopen the recorded report. | The native probe injects the focus-loss event during return, confirms the retained claim, and verifies the report bytes and re-audit link in the journal. An earlier interrupted run in the same probe uses **Return to berth** to release the claim. |
| Graphics context recreation during a live session | Stop active work during rebuilding and return to a usable view. | Native operations tour exercises rendering recreation and continues. Physical graphics-driver resets are unverified. |

The desktop takes its operations folder from a startup argument. There is no file-selection dialog in that workflow. Native typing and text-field cancellation, OS-level disk exhaustion, user cancellation during an active save, interruption within the database commit itself, physical graphics resets and clean-machine verification remain open. The tests use SQLite's real capacity and contention errors with bounded, disposable fixtures. The earlier native test run exposed incidental desktop focus events interrupting a probe. Native probes now ignore incidental focus transitions and explicitly inject the event under test.

## Commands and results

| Command | Result |
| --- | --- |
| `tools/build-windows-package.ps1 -NativeUi` | 133/133 Release tests passed, including the fault probes. |
| `tools/test-windows-package.ps1 -NativeUi` | 24/24 passed. The extracted app rejected a file as an operations directory and protected a damaged journal while simulation remained available. |
| `cmake --workflow --preset windows-debug` | 131/131 passed. |
| `cmake --workflow --preset linux-ci` | 130/130 passed with Clang 18, clang-tidy, ASan, UBSan and leak detection. |
| `cmake --build --preset linux-tsan --parallel 2` and `TSAN_OPTIONS=halt_on_error=1 out/build/linux-tsan/argus_unit_tests --gtest_filter="MissionStore.Concurrent*:SharedFleet.*:AisleAccess.*"` | Eight tests passed with no ThreadSanitizer diagnostics. |
| `cmake --build out/build/windows-phase-11-analysis --target argus-headless argus-desktop --parallel 2` | MSVC `/analyze` passed. |
| Tracked C++ clang-format check, PowerShell syntax check and `git diff --check` | Passed. Project target dependency declarations retain the existing acyclic graph. |
| `argus-headless --warehouse small-backroom --sensor-steps 220` | Phase 23 and Phase 24 archives produced byte-identical stdout with SHA-256 `0abf0712b66b6ec7412dee88698c7c62c49bf616745126d6812803f863a1e7ac`. |

The Windows ZIP in `out/release/phase-24` has SHA-256 `806504361685ff4471c4f73ba17b0be76606b0d6153970b8863f51449cb93168`.
