# 0013 Derive live map status from observed data

**Status** Accepted for Phase 13

## Context

The desktop drew the last accepted LiDAR scan without showing its age. Navigation reasons were visible in Diagnostics as machine labels. A long-lived occupancy map and a fresh observation serve different purposes during sensor interruptions.

## Decision

Use the navigator's existing sample-age limits for desktop status. Publish the sensor age and localization quality from the accepted scan, mapper estimate and public robot telemetry. Show navigation state, reason and the cumulative yield and replan counters in Live Map. Record the last replan trigger at the point where the navigator counts a replan.

Withhold LiDAR observations at the desktop simulation intake when the operator enables the dropout control. Continue wheel and RFID processing. Let the existing navigation freshness gate handle the fault. Suppress stale rays and preserve accumulated map evidence.

## Consequences

The display expresses sample time, estimated error bounds and the most recent navigation decision. Data that arrives later in the current tick can appear before the next navigation decision. The fault control is specific to the foreground simulation. It adds no hardware interface or simulator truth dependency to the operations model.
