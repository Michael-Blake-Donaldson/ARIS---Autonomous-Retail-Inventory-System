# Separate simulated sensor generation from observations

**Status** Accepted

## Context

Phase 4 adds information that the robot could observe while operating with imperfect hardware. Future mapping and inventory modules need timestamps, estimates and loss signals. The simulator owns the exact geometry and tag locations.

## Decision

Keep the physical robot pose and warehouse query inside sensor generation. Publish typed wheel, LiDAR and RFID observations with a shared header anchored to the estimate at acquisition. Use an independent clock for observation age and a per-sensor random stream. Model wheel error before pose integration, raycast against planar bounds for LiDAR, and use a bounded directional, dwell-based detection model for RFID.

Use one bounded FIFO per rig with drop-oldest backpressure and produced, consumed and dropped counts by sensor. Constrain beam count, cadence, maximum range, RFID read count and dwell using validated configuration. Supply only observed fields to callers. Retain the quantization-only headless drive path for Phase 3 baselines.

## Consequences

The rig is single-threaded and belongs to one simulation run. Queue drops are visible to consumers. The simplified RFID propagation, 2D geometry and wheel error budgets model uncertainty without assuming calibrated hardware. The phase adds no dependency.
