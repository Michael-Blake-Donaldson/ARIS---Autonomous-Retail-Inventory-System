# 0009 Fence mission work with durable state and revisions

**Status** Accepted for Phase 9

## Context

The robot must coordinate scans, searches, docking, and human inspection while preserving work across interruption. A cancellation, storage fault, or stale worker must never create false completion or authorize more work.

## Decision

Use a typed transition table, a materialized mission record, and an append-only state journal in a dedicated SQLite database. A single-active constraint, idempotent submission, and immediate write transactions serialize dispatch and state changes. Update checkpoints only for the active revision. Complete scan work only after the planned coverage is durably present.

Evaluate battery, observed navigation constraints, and sensor readiness before issuing motion. A higher-priority job is admitted after the active worker pauses and brakes. Docking the returning robot pauses preempted work or completes a dock-only mission. Recovery revokes the active revision and records a localization requirement. Replanning after resume uses a fresh observed pose and a stable per-view coverage mask.

The simulator adapter composes the observed mapper, Phase 6 navigator, and Phase 8 configured viewpoints. The scheduler and its store consume typed readiness and progress independently of the physical simulator.

## Consequences

Revision fencing protects durable evidence and the next command. Cancellation cannot retract a command already applied to an actuator. Storage pressure or invalid state leaves the last committed checkpoint and requires safe recovery. The simulation runner supplies an observed reset pose, while a physical adapter would need its own trustworthy localization.
