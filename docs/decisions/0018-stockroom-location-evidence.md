# 0018 Audit whole shelves and Wacos as separate locations

**Status** Accepted for Phase 19

## Context

The original fleet demo attached one stock location to each robot and covered one shelf per aisle. An expanded backroom needs stacked shelf levels, optional Wacos within a shelf and independent progress for each auditable location. A broad RFID read can reach across location boundaries.

## Decision

Use a four-aisle authored fleet layout with three stacked levels in each aisle. A20J06 has ten Wacos. Each whole shelf and each Waco is a leaf with an independent catalog scope, job, and history. The parent of a divided shelf shows Waco completion and contributes no extra audit. Robot selection and location selection are independent and either free robot can take a leaf. The fleet workers retain their original brake and interruption behavior.

Use a bounded steerable reader field in sensor generation. The robot aims it at a configured location from observed poses. The focused reader emits EPCs and signal samples. Use two separated views and require both views to contain the same EPC and isolate the location before counting it. Within a divided shelf, the lateral footprint includes the mapper's position and heading error. A whole shelf uses clearance between distinct racks and the focused elevation window. Observations that fail isolation go into the report's unresolved list. Longer transfers use authored cross-aisle staging points with observed navigation at each leg.

Keep the prior single-robot world and default sensor configuration. Fleet uses its own expanded scenario, fictional catalog and maintenance scene. Migrate the mission journal to schema 4 for the optional Waco suffix and retain existing shelf report rows. Read report versions 1 and 2. Version 2 stores unresolved EPCs separately from observed counts.

## Consequences

The main dashboard can report completion and review by aisle. Maintenance can inspect the selected robot with the full sensor visualizations. A report may be complete and still require review. The focused reader is a simulation model and can leave Wacos unresolved when the observation footprint overlaps adjacent slots. The migration keeps the existing quota and transaction. A journal too large to migrate safely remains on its prior version.
