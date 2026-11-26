# 0019 Advance fleet robots on one stockroom clock

**Status** Accepted for Phase 20

## Context

The fleet map showed two observed poses. Each robot previously advanced a separate instance of the stockroom. They could appear to meet on the map without being able to observe each other or share responsibility for access to an aisle.

## Decision

Use one immutable warehouse layout and two independently seeded robots. Each desktop worker owns its robot between ticks. The fleet clock waits for both commands, checks the proposed motion together and commits both steps under the same lock. If a moving actor would enter either robot's footprint, the world can hold its time while both sample clocks continue. The physics layer keeps true poses private. Sensor generation uses the peer footprint as an occluder for LiDAR and RFID.

Use a two-participant reservation service for traffic. Each request covers a padded span of the shared cross-aisle using the observed position and authored route endpoints. The earliest overlapping request goes first. Disjoint requests can move together. The waiting robot stays at its berth. A cancelled pending request releases its claim. An active request keeps its claim until the robot is stopped within the berth's clearance bound, including localization error. The operator can request an observed return to berth if a job stops along the route.

## Consequences

The fleet test can show a real yield in one shared stockroom and preserve the maintenance view for investigation. Reservations are deliberately coarse and can serialize work whose detailed paths could coexist. The simulation can brake both robots on predicted contact, with recovery handled by the observed navigation path and the operator controls. Closing the clock wakes a worker waiting for the other robot. Each robot retains its own sensor state, estimated map and audit provenance. The single-robot clock and stored audit format are unchanged.
