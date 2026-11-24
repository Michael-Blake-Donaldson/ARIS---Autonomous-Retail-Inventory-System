# 0006 Use an observed costmap and an independent LiDAR stop check

**Status** Accepted for Phase 6

## Context

A warehouse mission must discover free space with noisy observations, respect the robot footprint, yield to moving actors, and keep enough charge to return to the configured dock or observed launch position.

## Decision

Use occupied-cell inflation and a known-free costmap for eight-connected A*. Apply the same edge constraints when scoring frontiers and reserving the shortest return path. Use raw odometry and recent LiDAR returns for the local braking corridor. Compact clusters with consistent observed motion can trigger a yield before entering that corridor. The map estimate determines global waypoints, while recent scan returns can veto a command.

Keep navigation downstream of mapping and core. The CLI supplies the footprint and mission destination. A test-only evaluator measures actual pose, static reachability, actor sweep and simulator safety interventions through explicit truth interfaces.

## Consequences

Conservative inflation and scan age limits stop the robot when a narrow route or localization becomes uncertain. Temporary actor evidence can block a route until free-space observations update it. The tracker can lose association on occlusion or report motion near broken obstacle edges. The charging waypoint must be known in the map frame, and contact telemetry determines successful docking.
