# Autonomous exploration

Run a 20 Hz autonomous survey with `argus-headless --warehouse small-backroom --explore-steps 2000`. The same bounded probe runs through the desktop executable without opening a window. Use `--scenario-stdin` for a validated scenario supplied on standard input. The probe accepts 1–2,000 steps and a grid resolution of 50–2,000 mm, subject to the mapping limits.

Supply `--dock-mm X Y` with positive east and north millimeter coordinates to configure a charging waypoint. Omit it to use the first reliable observed position as the return point. The simulator confirms charging contact through normal robot telemetry. JSON output includes map classification counts, planner and yield counters, the last controller decision, and the exit reason. An exhausted step budget is reported as `step_limit`. Coverage against simulated geometry is available through the test evaluator.

## Interfaces and configuration

| Interface | Behavior |
| --- | --- |
| `Costmap::build` | Takes a mapper grid, circular footprint radius in meters, and up to 32 bounded no-go rectangles in map coordinates. Occupied cells, the outer perimeter, and no-go cells receive footprint clearance. Unknown cells are excluded from routes. |
| `plan` | Returns an inclusive A* route or no route. Cardinal and diagonal transitions cost 10 and 14 units. Both orthogonal neighbors must be traversable before taking a diagonal. |
| `select_frontier` | Chooses a reachable free cell bordering unknown space. It ranks information gain in a 5×5 neighborhood against outbound path cost. When a return point is supplied, the available budget must cover both outbound and return paths. |
| `Navigator::create` | Binds the navigator to the initial grid specification, a footprint, optional dock, a fixed battery margin and energy-per-meter estimate, and mission no-go rectangles. |
| `Navigator::set_destination` | Plans a directed position and heading for a scan view. Success or an inaccessible route ends that leg. Successive legs retain the launch point and return reserve. |
| `Navigator::update` | Takes the current mapper, controller telemetry, and most recent accepted full-sweep LiDAR scan. It emits a velocity command or releases the deadman. Construct a fresh navigator after resetting or reanchoring the mapper. |

The default planning reserve is 250 J plus 80 J per estimated route meter. The navigator enters return mode on low battery, insufficient round-trip margin, or loss of the known route to the return position. Reaching the configured dock requires hardware-model contact confirmation. The launch-point fallback ends after the robot returns to the observed position.

## Motion and failure behavior

- The follower caps linear speed at 0.55 m/s and rotation at 1.4 rad/s. It checks current free evidence, the inflated map, intermediate braking-path cells, and recent LiDAR returns. Braking-distance calculations use the simulator default of 1.8 m/s².
- The latest scan may be at most three ticks old. Invalid LiDAR, lost wheel updates, and exceeded localization uncertainty release the deadman. Emergency stop and depletion end the mission.
- Compact LiDAR clusters are associated across consecutive scans in odometry coordinates. Two consistent motion estimates qualify a cluster for forward projection. Worker, cart, and other returns all use the same geometric checks.
- Persistent blockage triggers a fresh plan. After a frontier remains blocked through the retry window, it can be excluded. A return path has at most three obstruction-driven retries. When the current cell becomes nontraversable or a return route cannot be found, the navigator waits briefly for a map update, then reports `unsafe_position`, `home_unreachable`, or `dock_unreachable`.
- `no_frontiers` indicates the current grid has no remaining frontier. `unreachable_frontiers` indicates that any apparent remaining frontier is unavailable or has been excluded. The search never drives into unknown cells to resolve that condition.

The navigation target links only to mapping and core. The CLI composes the mapper, simulator, sensor rig, and navigator. Simulated geometry, actor trajectories, and physical pose are used only to generate observations, enforce simulation physics, or score the offline evaluation. The proximity checks and energy budgets are calibrated for the supplied simulator and fixed-step drive model.
