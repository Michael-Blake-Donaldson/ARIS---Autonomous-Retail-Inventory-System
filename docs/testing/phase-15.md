# Phase 15 performance verification

**Date** November 25, 2026

**Baseline** `c3bdc64` for instrumented native rendering and `25e0106` for headless simulation. **Candidate** `8b89c53`.

## Headless simulation

The scale evaluation ran 2,000 fixed 50 ms simulation steps per trial with fixture seed 5438, two moving actors, two populated racks, and periodic sensor dropout. Timings cover the robot step, simulated sensor capture, observation handling, and the mapper. Setup is measured separately. RFID reads, truncated reads, queue drops, dropout ticks and safety holds matched within each catalog size. There were 140 dropout ticks and zero RFID queue drops for all sizes.

The table uses medians from five matched pairs after build and test activity ended. The first three pairs ran alongside quality checks and are retained separately in the raw evidence. Each measurement used one WSL virtual CPU.

| Tags | p95 ms before → after | p99 ms before → after | Peak RSS KiB before → after | RFID reads | Truncated detections |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 0.340 → 0.331 | 0.535 → 0.492 | 5,864 → 6,076 | 45,686 | 163,265 |
| 5,000 | 1.767 → 1.766 | 2.454 → 2.727 | 12,328 → 12,332 | 47,056 | 990,796 |
| 10,000 | 3.711 → 3.669 | 4.530 → 4.807 | 20,604 → 20,568 | 47,537 | 2,027,012 |

At 10,000 tags, the median wall time for 2,000 steps was 1,899.6 ms before and 1,904.9 ms after. The p99 medians rose at the two higher tag counts and the per-trial ranges overlapped. At 10,000 tags, p99 ranged from 4.167–5.022 ms before and 4.530–5.169 ms after. The measured optimization is in rendering. The separate glibc profile of the 10,000-tag, 2,000-step run measured 859,177 allocations, 48,764,840 total allocated bytes and a 13,638,501-byte peak heap per process in both builds, including scenario loading.

## Native Windows rendering

The Release `--ui-smoke` tour alternated baseline and candidate runs in three pairs. These are medians of the individual runs. The active frame includes foreground simulation work, dashboard drawing and presentation, excluding deliberate pacing. Scene timing covers `Scene::draw` on the CPU, including its graphics calls, before the dashboard draw and swap.

| Metric | Before | After |
| --- | ---: | ---: |
| Active frame p95 (ms) | 1.0044 | 0.9298 |
| Active frame p99 (ms) | 1.5843 | 1.4617 |
| Scene p95 (ms) | 0.3972 | 0.3465 |
| Scene p99 (ms) | 0.6746 | 0.6873 |
| Frames drawing occupancy | 786 | 787 |
| Geometry builds | 786 | 215 |
| Vertices generated | 4,223,580 | 1,153,156 |

The native tour generated about 73% fewer occupancy vertices. Scene p99 varied across runs and the median was slightly higher. All six tours verified observed motion and truth isolation, LiDAR dropout and recovery, continuous routed work, saved-run review, resize, and OpenGL context recreation. Each drew at most 12 catalog rows. The candidate also passed `--ui-smoke DIRECTORY` and `--ui-reopen DIRECTORY` in separate processes.

## Isolated occupancy geometry

The evaluation draws from two observed grids with 1,000,000 cells each. It advances the map revision every eight frames for 1,200 total frames. The workload exercises the renderer's existing stride of eight cells. Timings cover the CPU geometry build or cache lookup. The benchmark counts the vertices available for submission and performs no GPU calls. Values below are medians of five trials for each mode.

| Metric | Rebuild every frame | Cache by revision |
| --- | ---: | ---: |
| Geometry builds | 1,200 | 150 |
| Vertices generated | 60,002,400 | 7,500,300 |
| Vertices available for submission | 60,002,400 | 60,002,400 |
| p95 mesh work (ms) | 0.189 | 0.104 |
| p99 mesh work (ms) | 0.423 | 0.174 |
| Total mesh work (ms) | 125.30 | 16.76 |
| Ending geometry checksum | `8995353252066550313` | `8995353252066550313` |

Existing buffer capacity is reused in both modes. glibc recorded 94 allocation calls, 5,188,718 bytes allocated in total and a heap peak of 4,656,192 bytes per process in both modes. These process totals include building the two input grids.

## Cache measurements

Cachegrind 3.22.0 modeled a 32 KiB instruction cache, 48 KiB L1 data cache and 8 MiB last-level cache with 64-byte lines. Counts include program setup. Over 1,200 mesh frames, modeled L1 data-cache misses fell 86.3% as repeated geometry work was removed. Modeled last-level data-cache misses were essentially unchanged.

| Workload | Data accesses | Modeled L1 data misses | Modeled last-level data misses |
| --- | ---: | ---: | ---: |
| Mesh, rebuild every frame | 630,173,561 | 17,648,544 | 84,885 |
| Mesh, cache by revision | 87,309,578 | 2,409,802 | 84,880 |
| Headless candidate, 10,000 tags and 150 steps | 2,255,126,501 | 16,765,649 | 547,212 |

The WSL kernel did not expose a CPU PMU and a process-local `PERF_COUNT_HW_CACHE_MISSES` probe returned `ENOENT`. Windows listed the `CacheMisses` source, and the current session lacked elevation for the Windows Performance Recorder profile. Hardware and GPU cache counters remain unmeasured. The explicit Cachegrind model, including its last-level size, differs from the physical host. [Cachegrind cache model](https://valgrind.org/docs/manual/cg-manual.html) · [Windows PMU source recording](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/recording-pmu-events) · [Windows Performance Recorder permissions](https://learn.microsoft.com/en-us/windows/win32/tracelogging/tracelogging-record-and-display-events)

## Behavioral parity

The before and after headless executables produced byte-identical outputs for three `small-backroom` seeded commands. The SHA-256 digest is over each complete output file.

| Headless arguments | SHA-256 |
| --- | --- |
| `--sensor-steps 220` | `882788058dd2c853e386c003f00d386751fd684647081875b9dfc8514830c2ac` |
| `--export-map 220` | `359c63ef4d8654073d952277b2f403addd4fd3d31308c5532e40020f78203d6c` |
| `--explore-steps 1200` | `8e9e059320661160ec70c806da7dac0fab599f9d7a9537fbf493221dbec77fa2` |

Mapper tests exercise invalidation for scans, clears and rejected observations. The renderer tests compare vertices against the uncached builder and exercise worker copies, skipped revisions, run and grid changes, archive transitions, and explicit rebuilds. The cache consumes bounded observed grids and saved grids accepted by the existing archive validator. The links between 22 declared CMake project targets are acyclic.

## Reproduce the measurements

The native host was Windows 11 Pro build 26200 on an Intel Core i7-13700F and NVIDIA RTX 4070 with driver 32.0.16.1742. Linux measurements used Ubuntu 24.04 on WSL2 with Clang 18. Run instrumented native builds on the same interactive host and display. Use the Visual Studio x64 developer environment and the repository-pinned vcpkg installation for Windows.

Build Windows Release separately at the two native revisions and alternate their `argus-desktop.exe --ui-smoke` runs. For the durable workflow, use an existing disposable directory as the optional argument to `--ui-smoke` and launch a second process with `--ui-reopen DIRECTORY`.

On Linux, configure optimized builds of the baseline and candidate into separate build directories. `BUILD` below is the chosen build directory and `VCPKG_ROOT` points to the repository-pinned installation.

```bash
cmake --preset linux-ci -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DARGUS_ENABLE_SANITIZERS=OFF -DARGUS_ENABLE_CLANG_TIDY=OFF
cmake --build "$BUILD" --target argus-headless argus-scale-evaluation
cmake --build "$BUILD" --target argus-occupancy-evaluation # Candidate

for tags in 1000 5000 10000; do
  /usr/bin/time -v taskset -c 4 "$BUILD/argus-scale-evaluation" "$tags" 2000
done
for mode in uncached cached; do
  taskset -c 4 "$BUILD/argus-occupancy-evaluation" 1200 "$mode"
done

taskset -c 4 env MEMUSAGE_NO_TIMER=1 LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libmemusage.so \
  "$BUILD/argus-scale-evaluation" 10000 2000
for mode in uncached cached; do
  taskset -c 4 env MEMUSAGE_NO_TIMER=1 LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libmemusage.so \
    "$BUILD/argus-occupancy-evaluation" 1200 "$mode"
  taskset -c 4 valgrind --tool=cachegrind --cache-sim=yes \
    --I1=32768,8,64 --D1=49152,12,64 --LL=8388608,16,64 \
    --cachegrind-out-file="mesh-$mode.cg" "$BUILD/argus-occupancy-evaluation" 1200 "$mode"
done
taskset -c 4 valgrind --tool=cachegrind --cache-sim=yes \
  --I1=32768,8,64 --D1=49152,12,64 --LL=8388608,16,64 \
  --cachegrind-out-file=scale-10000.cg "$BUILD/argus-scale-evaluation" 10000 150
```

For replay, run the arguments in the parity table with `--warehouse small-backroom` before the workload flag, redirect stdout to separate files, and compare the baseline and candidate with `cmp`. Use binary-safe shell redirection for the map file.

The first candidate benchmark build used a configuration generated before the new target existed. Reconfiguring the Linux Release tree exposed the target and the rebuild passed. Cachegrind emitted a Valgrind heap reservation warning during the scale run and completed the replay with a full summary.

## Quality gates

| Command | Result |
| --- | --- |
| `cmake --workflow --preset windows-debug` | 112/112 tests passed. |
| `cmake --workflow --preset windows-release` | 112/112 tests passed. |
| `cmake --workflow --preset linux-ci` | 111/111 tests passed with Clang 18, clang-tidy, ASan, UBSan and leak detection. |
| `cmake --build out/build/windows-phase-11-analysis --target argus-headless argus-desktop --parallel 2` | Built successfully using MSVC `/analyze`. |
| `argus-occupancy-evaluation` with `7 cached`, `10001 cached` and `8 other` | Each rejected the input with exit code 2. |
| `git ls-files -z -- '*.cpp' '*.hpp' '*.hpp.in' \| xargs -0 -r clang-format-18 --dry-run --Werror` and `git diff --check` | Formatting and whitespace checks passed. |

Raw measurements and replay outputs are under `out/evidence/phase-15` on the profiling host. Timing was recorded on one machine, and the results do not establish a latency target for other systems.
