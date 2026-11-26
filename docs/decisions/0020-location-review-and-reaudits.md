# 0020 Keep review status separate from audit evidence

**Status** Accepted for Phase 21

## Context

The dashboard previously flagged a location whenever its latest report contained differences or uncertain RFID evidence. A lead could read the findings, but the dashboard could not record an acknowledgment or show the relationship between two audits of the same location.

## Decision

Review applies to the latest completed report at a location. Its timestamp is stored beside the report, with the report payload and provenance preserved. A request names the location and the exact robot, run and session. The journal checks that identity under a write transaction, and repeated acknowledgments of the current report return the original timestamp. The desktop performs one review write at a time away from the render thread.

Capture the current report's identity when a re-audit is queued. Save the new report and source identity together in the existing audit transaction. The source must still be the latest report for that location when the re-audit is saved. Review starts fresh for the new report when it has findings. Comparison uses the saved reports' DPCIs and shows both counts for each snapshot. An absent product appears as a dash so it can be distinguished from a recorded zero.

## Consequences

The migration extends existing journal rows without changing report payloads. The journal keeps at most 20 audits per location. A linked report retains its source identity when retention removes the source contents. Reviews are local acknowledgments without user accounts or changes to the inventory catalog. An uncompleted re-audit can be queued again after a hold or restart.
