# 0022 Protect audit history when storage fails

**Status** Accepted for Phase 24

## Decision

Reject invalid operations directories before starting graphics. Keep the selected path fixed for the session. If saved fleet history cannot be read or validated, show the problem and disable new audit and review writes for that session. Keep available evidence and diagnostics accessible. Reopen ARGUS after the folder or journal is recovered.

Distinguish database contention, exhausted capacity and read-only storage where SQLite reports them. The write transaction owns rollback. Operator guidance describes how to retry or recover and avoids printing local paths. Use a bounded journal quota and a second connection to exercise actual SQLite storage failures in tests.

Process interactive window focus-loss events when they arrive. A subsequent focus gain can occur before the frame samples the window state. Native verification injects the same event without depending on desktop focus timing.

## Consequences

Malformed journal data requires recovery before recording more audits in that directory. A database capacity failure can be reproduced inside a disposable test without filling the development drive. Drive and fleet simulation remain available for diagnostics when the fleet detects unreadable audit history.
