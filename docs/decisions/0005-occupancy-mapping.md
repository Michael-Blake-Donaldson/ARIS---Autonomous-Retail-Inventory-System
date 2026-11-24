# Use an estimated occupancy grid with explicit pose recovery

**Status** Accepted

## Context

Phase 5 needs to project measured ranges into a reusable map. Wheel error can corrupt that map, and consumers need an explicit reliability signal.

## Decision

Use saturated integer log-odds evidence and a bounded grid traversal. Gate each LiDAR update on a matching, reliable wheel observation. Register any independent pose anchor against current odometry and clear earlier map evidence when accepting a new anchor. Save versioned, checksummed evidence with an explicit scenario, frame and resolution.

Build the mapper against the public observation types. Put all comparisons to physical pose or warehouse geometry in a separate test target. In the Windows preview, the occupancy mode renders estimated state only.

## Consequences

Missing returns leave the evidence unchanged. Pose uncertainty can interrupt mapping while the robot continues to move. The model uses wheel odometry and an explicit recovery interface. It adds no dependencies.
