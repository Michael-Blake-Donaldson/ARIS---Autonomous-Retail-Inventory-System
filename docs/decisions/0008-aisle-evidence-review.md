# 0008 Attribute RFID evidence to aisles and preserve review cases

**Status** Accepted for Phase 8

## Context

Repeated RFID messages can come from the same item and same receiver view. Radio signals can also cross aisles or fail to reach a present tag. Inventory decisions need to preserve those uncertainties.

## Decision

Use independently configured aisle regions and paired viewpoints. The simulator adapter supplies sensor observations and odometry to navigation and records them in SQLite. The audit target aggregates evidence without any simulation geometry or tag location API. The navigator preserves its launch point and energy reserve across successive destinations.

Count one EPC per session. Keep a representative antenna footprint per view and preserve individual reads in the repository. Require agreement across independent views before supporting an aisle. Run an alternate pass where unresolved EPCs and first-pass evidence warrant it. Keep absence of a read distinct from coverage and independent confirmation.

Export structured findings and an exception review queue with stable session-and-EPC IDs. Official expected quantities and assignments remain independent from findings. Review dispositions are recorded in the exported worksheet after independent verification.

## Consequences

The score ranks evidence for an aisle and is not calibrated as a probability. A visit skipped for safety contributes no coverage. Configured viewpoint quality affects the result. A new warehouse layout needs its own operational aisle regions and viewpoints.
