# Simulated sensor models

The robot measures at 50 ms intervals. Each observation carries its sensor ID, run-relative sample tick, simulated timestamp, warehouse tick, estimated robot pose and simulated provenance. The sample clock advances even when collision handling holds the warehouse tick. A rig accepts each step once, in order.

| Sensor | Default cadence | Data and quality |
| --- | --- | --- |
| Wheel encoder | 20 Hz | Left and right distance increments, cumulative distance and heading error budgets, and power availability |
| LiDAR | 10 Hz | 180 bins across 360°, up to 12 m, nullable range per bin, return count and configured range error |
| RFID | 10 Hz | Fictional EPC and relative signal strength for each decoded read, plus a count of reads truncated by the scan limit |

`argus::sensors::Observation` in [observations.hpp](../include/argus/sensors/observations.hpp) is the consumer contract. `argus::simulation::SensorRig` owns the generators and uses the simulator's private warehouse and robot state. The desktop F4 overlay uses the measured ranges and the estimate recorded with that scan.

## Wheel odometry

The simulator applies persistent, independently seeded wheel scale bias and a bounded random perturbation to accepted wheel travel. Each moving wheel can slip independently. The default profile uses up to 0.3% scale bias, up to 0.6 mm jitter per moving step, and a 4% chance of a 12% travel loss per wheel. Wheels are quantized to 1 mm with a retained sub-pulse remainder. The integrated odometry affects the estimate and leaves physical motion and collision handling in the simulator.

Distance and heading error budgets accumulate from reported pulses and configured bounds. These are conservative travel and rotation indicators. They can increase while stopped and cap at a finite reporting limit. They carry no covariance or statistical calibration.

## LiDAR

Each beam starts at the robot center. A planar ray intersects the nearest rectangle or the warehouse boundary. Beams use equal-width angular bins and report their centers. Bin spacing and the first bearing are relative to the sensor heading. Default returns can have up to 12.5 mm uniform range error and a 2.5% chance of a missing return. A null range covers either a missing detection or geometry beyond the configured range.

The API configures 1–720 beams, field of view up to a full circle, 0.1–50 m range, 0–0.5 m bounded noise, a missing-return probability and a cadence of 1–120 steps.

## RFID

Antenna offsets and yaw are relative to the robot. Range uses the antenna and shelf heights. A tag must stay in the antenna cone and range over the configured number of consecutive interrogations before a detection attempt. Capture gaps and loss of power clear dwell. The default antenna faces forward with a 0.2 rad yaw, uses an 8 m range and a 180° cone, and requires two interrogations.

Read probability accounts for distance, off-axis gain, tag orientation and intervening geometry. Each crossed obstacle reduces the probability and signal strength. The host rack is excluded from intervening geometry. The model can miss nearby tags, decode tags through a divider and read the same EPC on successive scans. Signal strength is a simulated heuristic for inspection. It has no calibrated mapping to shelf location.

The default limits a scan to 64 decoded reads and counts any further detections as truncated. There is no synthetic EPC generation. A source scenario supplies every tag ID.

## Replay and backpressure

`--sensor-steps N` runs 1–2,000 steps. The CLI drains the retained queue at the end and reports the backlog before draining alongside the three counters per stream. The desktop drains the queue after each step. LiDAR and RFID stop acquisition when disabled or depleted. Wheel telemetry points carry their power state.

The queue holds up to 24 observations by default and can be configured from 1 through 64. It drops the oldest observation on overflow and records which sensor lost it. Consumers can use the counters and sample ticks to detect loss. Wheel, LiDAR and RFID use independent random streams derived from the scenario seed.
