# 0021 Preserve audit evidence through fleet recovery

**Status** Accepted for Phase 23

## Decision

Commit a completed survey before dispatching the return to berth. The report retains the survey completion time, robot, run, session and re-audit link. A failed or interrupted return leaves the report available for review. The aisle reservation is released separately when current localization places the stopped robot within the berth clearance bound. The operator can request clearance again.

The first submission for each shared tick starts a five-second deadline measured with the monotonic clock. Both commands must arrive before that deadline. A timeout closes the fleet session with a specific reason and applies neither command for that tick. Fleet keeps reports accessible and disables motion until the application is restarted. Late submissions cannot reopen the closed session.

## Consequences

The deadline allows the normal 50 ms simulation rhythm and the configured 1.5-second SQLite contention wait. It also bounds how long a healthy worker waits if its peer never reaches the clock. This mechanism cannot force a worker out of a blocked operating system call. The simulation preserves the last jointly committed state when the clock stops.
