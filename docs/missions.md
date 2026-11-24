# Mission orchestration and recovery

Use a separate local SQLite file for the mission queue and journal. An inventory database can be supplied to targeted search as catalog input.

```bat
out\build\windows-release\argus-mission.exe out\missions.db submit 901 scan-all routine
out\build\windows-release\argus-mission.exe out\missions.db run 1600 -
out\build\windows-release\argus-mission.exe out\missions.db report 901
```

Use `out/build/linux-ci/argus-mission` on Linux. `run` dispatches one pending mission in the `rfid-audit` simulation. Use a positive mission ID up to the largest signed 64-bit value. Resubmitting an identical ID and intent returns the existing record. A changed intent under the same ID returns `conflict`.

| Workflow | Submission arguments | Behavior |
| --- | --- | --- |
| Scan all | `scan-all routine` | Visits both viewpoints for each configured aisle and records qualified coverage and read counts. |
| Targeted search | `find-item urgent 990-01-0001 SOFTLINES` | Uses a catalog passed to `run` in place of `-`. Prioritizes the optional aisle hint and catalog assignments, then reports unresolved target EPCs. |
| Dock | `dock safety` | Follows the estimated return route and completes when charging contact is confirmed. |
| Manual inspection | `inspect urgent TECH` | Holds the robot stationary and leaves an open inspection request. After performing the inspection, use `inspect-confirm ID`. |

Routine and urgent work share a bounded queue. Safety priority is reserved for explicit dock work. Equal-priority missions dispatch in submission order. An active worker checks for priority work while acquiring its next command. It brakes and pauses before handing off to a higher priority. Returning holds dispatch until dock arrival, cancellation, or a safety stop. `cancel ID` invalidates the active lease. Scan checkpoints committed before cancellation remain in the journal. A worker carrying the old lease cannot publish a later checkpoint.

## Persistence and restart

The journal holds at most 4,096 mission records and 16 unfinished records. State changes and their append-only events commit in one SQLite transaction. A monotone revision identifies the active lease and reserves room for recovery and a terminal event. Checkpoints retain elapsed milliseconds, planned and measured odometry distance, the count and mask of qualified views, raw read count, and exceptions. A monotonically increasing checkpoint sequence rejects replay.

`run` accepts 1–2,400 simulation ticks. CLI submissions use a mission timeout of 100 seconds of accumulated simulation time. When a run's step budget ends with work still active, the final checkpoint stays active for inspection and recovery. The demo releases the deadman as it exits. To recover after an interrupted process, stop the old runner, use `recover`, then run with a resume ID. For example, `run 1600 - normal 901` reacquires a fresh observed launch pose in the synthetic fixture. The resumed mission skips durably completed viewpoints. Recovery pauses interrupted work, invalidates its old lease, and requires an explicit reliable localization proof before requeueing. An old checkpoint contains no position to reuse.

Only one actuator loop should own the robot. A previously dispatched command can still be in flight when cancellation commits. The runner checks its lease before each new command or checkpoint and brakes on exit. The CLI is a simulation runner. A physical adapter would need to re-localize independently and provide its own actuation watchdog.

## Fault and battery policies

| Condition | Policy |
| --- | --- |
| Worker obstruction | Brake, record a blocked transition, and replan after observed clearance. The navigation follower checks local LiDAR throughout a return. |
| Low battery or reserve threshold | Brake before authorizing another work step and enter Returning. The threshold includes the estimated route cost, a 250 J reserve, and the next-step allowance. On arrival, interrupted work pauses. |
| Unsafe or unknown return route | Withhold motion until the declared safe route is available or the mission times out. |
| Sensor or localization loss | Brake while acquiring initial readiness. Active work pauses so a fresh localization check can be made. |
| Storage failure | Withhold new commands. A failed transaction leaves the previous durable state. Recover it after storage is available. |
| Timeout or unreachable work | Brake and report failure. A queued or interrupted scan is never finalized as complete. |

The `reserve` run mode starts a deterministic low-energy test profile to exercise the margin. The postmortem uses A* route estimates for planned distance and wheel odometry for measured distance. The mission journal records coverage and counts. Use the Phase 8 inventory workflow for individual RFID evidence and review exports. Targeted search confidence after a restart is conservative because earlier per-EPC observations live outside the mission checkpoint.
