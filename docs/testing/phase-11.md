# Phase 11 security, correctness, and resilience verification

**Date** November 24, 2026

**Code under test** `6481283`

## Acceptance evidence

| Gate | Evidence |
| --- | --- |
| No known high-severity defects within the supported threat model | The phase review covered untrusted scenario, catalog and map bytes, local file and storage failures, resource budgets, secret handling and dependency intake. Tests verified corrupt snapshots and databases, unsafe export leaves, failed replacement and transactional rollback. [Local threat model](../security/phase-11.md) |
| Bounded sanitizer and fuzz runs | Windows and Linux test workflows passed. Linux ran ASan, UBSan and leak detection. All four libFuzzer targets completed their declared runs. ThreadSanitizer covered cancellation, RFID and queue cases. |
| Reproducible performance and fault evidence | Measured three tag densities, a 10,000-tick soak, allocation activity, native frame rate and completed mission throughput. The RFID change was measured before and after using the same seeded workloads. |

## Build and analysis

| Command | Evidence |
| --- | --- |
| `cmake --workflow --preset windows-debug` | 105/105 tests passed, MSVC 19.40.33811. |
| `cmake --workflow --preset windows-release` | 105/105 tests passed, MSVC 19.40.33811. |
| `cmake --workflow --preset linux-ci` | 104/104 tests passed with Clang 18.1.3, clang-tidy, ASan, UBSan and leak detection. |
| `git ls-files -z -- '*.cpp' '*.hpp' '*.hpp.in' \| xargs -0 -r clang-format-18 --dry-run --Werror` | Passed for tracked C++ and header templates. |
| MSVC project-code analysis | Built the headless, inventory, mission and desktop targets with `/analyze`, excluding external headers. No project analysis diagnostics were emitted. |
| CMake dependency graph | 21 project targets with acyclic link dependencies. The graph also lists Windows runtime-copy steps as references to their own targets. Mapping, audit logic and the operations model have no links to the simulation world or sensors. |
| `cmake --preset linux-fuzz && cmake --build --preset linux-fuzz && bash tools/run-phase11-fuzz.sh` | Coverage instrumentation on project objects and ASan/UBSan enabled. The four parsers ran 20,000 inputs each with seed 20261124, a 10-second per-input timeout and a 2,048 MiB RSS limit. |
| `cmake --preset linux-tsan && cmake --build --preset linux-tsan` | Seven tests passed across three repetitions with no TSan diagnostics. The race stress exercised 192 cancellation/checkpoint races, plus three ordered cancellation runs. |
| SBOM collection and `sha256sum -c SHA256SUMS` | Verified 10 Windows and 8 Linux SPDX 2.3 documents from the pinned builds. [Dependency and release review](../security/dependencies.md) |

Windows used the Visual Studio environment wrapper and the pinned vcpkg registry. The separate analysis used these commands.

```bat
cmake --preset windows-debug -B out/build/windows-phase-11-analysis "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /analyze /analyze:external- /external:anglebrackets /external:W0"
cmake --build out/build/windows-phase-11-analysis --target argus-headless argus-inventory argus-mission argus-desktop --parallel 2
cmake --preset windows-release --graphviz=out/evidence/phase-11-benchmark/windows-targets.dot
```

```bash
TSAN_OPTIONS=halt_on_error=1 out/build/linux-tsan/argus_unit_tests \
  --gtest_filter='MissionStore.Concurrent*:SensorQueue.*:Rfid.*' --gtest_repeat=3
```

## Parser budget and fault matrix

The fuzz harnesses use the production scenario decoder, CSV and JSON catalog decoders, map loader and typed RFID evidence intake. The RFID harness generates typed frames from mutated bytes and checks that rejection preserves the accumulator. Catalog and scenario inputs start from fictional fixtures, including valid CSV and JSON. CTest covers the byte and parser event ceilings, report rejection and the maximum-size tag scenario.

| Target | Configured maximum bytes | Inputs completed | Runtime | Final coverage counters |
| --- | ---: | ---: | ---: | ---: |
| Scenario JSON | 2,097,153 | 20,000 | 15 s | 1,550 |
| Catalog CSV and JSON | 524,289 | 20,000 | 15 s | 1,279 |
| Map format | 1,000,069 | 20,000 | 1 s | 165 |
| RFID evidence | 16,384 | 20,000 | 1 s | 230 |

All runs completed without crashes or sanitizer diagnostics. The final corpus growth limits were 2,139, 658, 2,519, and 64 bytes, respectively. These values describe the campaign's mutation lengths. The separate boundary tests cover larger inputs.

| Fault | Evidence and observed behavior |
| --- | --- |
| Sensor dropout | `SensorQueue.HonorsCadencePowerAndConfigLimits` and `Navigation.StopsOnExpiredScanAndRetainsReserveAtLaunchPoint` verify power/cadence and safe control with stale LiDAR. The scale runs powered sensors down for ten ticks in every 137-tick interval and drained the observation queue. |
| Corrupt saves | The map suite rejected truncated, altered, overlong and incompatible files. An independent SHA-256 vector matched, a wrong trusted pin was rejected, and a legacy map loaded. Failed file publication cleaned the temporary entry and preserved the blocked destination. The inventory store rejected a partial physical database page and an unexpected view. |
| Lost localization | `Mapper.SuspendsOnUncertaintyAndFollowsExplicitAnchorInNewFrame` checks suspension and explicit re-anchoring. `MissionStore.RecoveryRequiresRelocalizationAndPreservesUnfinishedWork` checks pause, stale lease rejection and validated resume. |
| Interrupted scan | The mission CLI stops after partial coverage, reopens the store, preserves the checkpoint and requires relocalization before completion. The race suite checks ordered cancellation and 64 concurrent cancellation/checkpoint transactions per repetition. |
| Blocked dock | `argus-navigation-evaluation` reports `dock_unreachable` with zero movement for its inaccessible return target. The mission state test exercises braking while return is unavailable. |
| Disk full | `InventoryRepository.StorageQuotaRollsBackEveryCatalogRowOnSqliteFull` reached the SQLite page limit with a valid 4,096-record catalog. The new product, bin and EPC rows rolled back. The connection then committed a small catalog successfully. The test uses a quota to avoid exhausting the host filesystem. |
| Oversized report and malformed import | The report caps reject an oversized object before writing. Catalog tests reject malformed UTF-8, malformed CSV, duplicate records and raw NUL. |

## Performance setup

- Host CPU was a 13th Gen Intel Core i7-13700F with 16 cores and 24 logical CPUs. Windows 11 Pro build 26200 reported 34,067,857,408 bytes of physical memory. The graphics device was an NVIDIA GeForce RTX 4070 with driver 32.0.16.1742.
- Linux ran in Ubuntu 24.04.1 LTS on WSL2 kernel `6.18.58.1-microsoft-standard-WSL2`, with 24 logical CPUs and 16,233,424 KiB reported memory. Build type was Release with Clang 18.1.3 and no sanitizers. The timings used WSL logical CPU 4 and the native Linux build directory.
- `argus-scale-evaluation TAGS TICKS` derives a 12 m × 8 m scenario from the fictional RFID fixture with seed 5438, 250 mm map cells, two configured moving obstacles, 180 LiDAR beams, a 64-read RFID frame cap and 50 ms simulated ticks. Timing includes robot stepping, sensor generation, queue drain and mapping. Setup time includes generation and import. The profiling run invokes glibc `libmemusage.so` separately from the latency runs, so allocator instrumentation does not affect the reported uninstrumented timings.

```bash
build="$HOME/argus-tools/phase11-release"
cmake --preset linux-ci -B "$build" -DCMAKE_BUILD_TYPE=Release \
  -DARGUS_ENABLE_SANITIZERS=OFF -DARGUS_ENABLE_CLANG_TIDY=OFF \
  -DVCPKG_INSTALLED_DIR="$PWD/out/build/linux-ci/vcpkg_installed"
cmake --build "$build" --target argus-scale-evaluation argus-mission
for tags in 1000 5000 10000; do
  for rep in 1 2 3; do
    /usr/bin/time -v taskset -c 4 "$build/argus-scale-evaluation" "$tags" 1000
  done
  MEMUSAGE_NO_TIMER=1 LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libmemusage.so \
    taskset -c 4 "$build/argus-scale-evaluation" "$tags" 1000
done
/usr/bin/time -v taskset -c 4 "$build/argus-scale-evaluation" 10000 10000
```

## Performance results

Uninstrumented latency and CPU figures are medians of three 1,000-tick runs per density. RSS is the highest process peak across the three runs. Allocation calls include scenario setup and use a separate instrumented run.

| Tags | CPU ms | Tick p50 ms | Tick p95 ms | Tick p99 ms | Ticks/s | Peak RSS KiB | Allocation calls |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 68.35 | 0.0009 | 0.225 | 0.277 | 14,630 | 6,076 | 94,459 |
| 5,000 | 292.86 | 0.0016 | 1.031 | 1.292 | 3,578 | 12,548 | 355,245 |
| 10,000 | 642.63 | 0.0023 | 2.205 | 2.709 | 1,662 | 20,660 | 680,638 |

The pre-optimization 10,000-tag run measured 4.720 ms p95 and 9,906,872 allocations. The final run reduced p95 by 53% and allocation calls by 93%. Visiting immutable truth inside sensor generation removes per-scan EPC copies. Reusing dwell entries removes repeated allocation while preserving scenario order and seeded sensor behavior. Read and truncation counts matched before and after at all three densities. Peak RSS remained close to 20 MiB because scenario loading still constructs the bounded JSON document.

The 10,000-tag, 10,000-tick soak covered 500 simulated seconds in 5.67 seconds of wall time. It measured 2.225 ms p95, 4.877 ms worst tick and 20,604 KiB peak RSS. It delivered 235,317 RFID readings with 9,715,180 detections beyond the frame cap, 729 sensor-off ticks and zero RFID queue drops. The simulated obstacles moved and the robot completed the sample sequence.

The Windows Release `argus-desktop.exe --ui-smoke` tour rendered 654 frames in 8.60 seconds, averaging 76.0 fps including pacing and startup. Foreground work measured 0.472 ms p95 excluding deliberate pacing. It painted at most 12 catalog rows, recreated its graphics context, and verified observation and truth-overlay scoping.

Ten normal scan-all jobs completed all four views in 4.81 seconds, or 2.08 jobs per second, using the Release mission CLI on a fresh native Linux SQLite file. The timed loop includes CLI startup, submission, execution and commits. It uses the standard mission fixture.

```bash
db="$(mktemp -d)/missions.db"
export ARGUS_MISSION_BENCH_DB="$db"
export ARGUS_MISSION_BENCH_EXE="$build/argus-mission"
/usr/bin/time -v taskset -c 4 bash -c '
  set -e
  for id in $(seq 1 10); do
    "$ARGUS_MISSION_BENCH_EXE" "$ARGUS_MISSION_BENCH_DB" submit "$id" scan-all routine >/dev/null
    "$ARGUS_MISSION_BENCH_EXE" "$ARGUS_MISSION_BENCH_DB" run 1600 -
  done'
```

The local run logs, SPDX exports and benchmark outputs are under `out/evidence/phase-11-*`.

## Limits and resolved failures

- Initial CLI smoke expected the pre-change help text. The compatibility tests now cover both the new help and a digest whose bytes include values above 127. An early database test showed SQLite could accept a partial last page containing padding. The application now checks physical page alignment through the opened SQLite file.
- The first Memcheck attempt lacked usable glibc loader symbols in WSL. The allocator tally uses glibc's allocation instrumentation. Peak process RSS is measured independently with GNU time.
- Scenario tag counts and truncated RFID readings characterize simulator load. Per-audit evidence and per-frame read limits continue to apply. Benchmarks use one host and one authored scale layout.
- The TSan run covers Linux project code and exercises SQLite through its packaged binary. Windows graphics-driver internals, power-loss durability, hostile filesystem administrators and hosted GitHub Actions are outside the local test evidence.
