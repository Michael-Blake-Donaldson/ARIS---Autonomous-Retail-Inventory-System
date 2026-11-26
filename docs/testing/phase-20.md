# Phase 20 shared fleet simulation verification

**Date** November 26, 2026

**Code under test** `4051366fedf75f46917d8c4aaf5cf57e18ca06ff`

## Acceptance

| Gate | Evidence |
| --- | --- |
| Shared clock and world | The physics test checks matching world and sample ticks for both robots. A scripted worker can hold the world clock for both while sensors and odometry continue sampling. The clock rejects independent world instances and releases both participants. |
| Peer sensing and physical separation | In the head-on test, the two robots approached the stockroom's 1.18 m passage. Both emitted close LiDAR returns from the encounter and braked. Explicit test diagnostics verified separation greater than the combined 0.60 m diameter throughout the run. |
| Yield and job completion | The native probe observed Robot 2 stopped with a visible **Yielding** status while Robot 1 audited `A20J05`. Robot 2 then moved and completed `A20J06-W01`. Both histories retained their robot IDs and neither robot held an aisle claim after returning. |
| Disjoint work, interruption and recovery | The native smoke test observed concurrent movement on disjoint routes. The reopen test verified the operator hold in Fleet. Unit tests verified that interrupting a pending request frees its claim, while interrupting an admitted robot outside its berth retains the claim until clearance. |
| Observation boundary and operator views | Traffic uses authored endpoints and observed estimates. Operational snapshots contain estimates, observed routes, LiDAR and status. Exact peer poses stay in physics and sensor generation. Both Maintenance views retained LiDAR and motion trails during movement. |
| Existing reports and seeded simulation | The native reopen test reopened both location reports in a fresh process. The Phase 19 and Phase 20 `small-backroom` sensor replays produced identical bytes with SHA-256 `0abf0712b66b6ec7412dee88698c7c62c49bf616745126d6812803f863a1e7ac`. |

## Commands and results

| Command | Result |
| --- | --- |
| `tools/build-windows-package.ps1 -NativeUi` | 126/126 passed in Release, including the three native fleet probes and the original operations tour. |
| `cmake --workflow --preset windows-debug` | 124/124 passed. |
| `cmake --build --preset linux-ci --parallel 2` and `ctest --preset linux-ci` | 123/123 passed with Clang 18, clang-tidy, address and undefined behavior sanitizers and leak detection. |
| `cmake --build out/build/windows-phase-11-analysis --target argus-headless argus-desktop --parallel 2` | MSVC `/analyze` passed. |
| `git ls-files -z -- '*.cpp' '*.hpp' '*.hpp.in' \| xargs -0 -r clang-format-18 --dry-run --Werror` and `git diff --check` | Passed. The CMake project link graph is acyclic, including optional fuzz targets. |
| `argus-headless --warehouse small-backroom --sensor-steps 220` | Byte-identical to the archived Phase 19 executable. |

The first placement of the narrow passage interfered with a Waco survey route. The walls moved north of the survey viewpoints, and the head-on test now uses that passage. The initial native contention pairing with `A20J07` also exceeded the probe's 35-second deadline. The verified contention scenario uses the whole shelf and the first Waco. A separate native run for the `A20J07` pairing and a scripted UI click through berth recovery remain open. The two-robot scheduler reserves coarse route spans, and the local Windows host supplies the native evidence.

## Reproduce

```powershell
$data = New-Item -ItemType Directory -Path (Join-Path 'out/evidence' ('fleet-traffic-' + [Guid]::NewGuid()))
out/build/windows-package/argus-desktop.exe --ui-fleet-traffic $data.FullName
```

For manual operation, open **Fleet**, run the two A20 assignments and watch **Yielding** switch back to motion. Use **Robot Maintenance** to inspect the selected robot. [Operations center guide](../operations-center.md#run-a-two-robot-shelf-audit)

## Local release

The Windows x64 ZIP in `out/release/phase-20` is unsigned and has SHA-256 `03956f8b299a18abc2cff3e78e2bd8e76bac149e491fc395ddf2888e3fbde935`. The acceptance receipt matches the package digest and tested commit. The extracted program passed 17 standalone package checks, including replacement and reopening without losing operator data. All three fleet probes also passed against the extraction with a system-only PATH. Fresh-machine verification remains open.
