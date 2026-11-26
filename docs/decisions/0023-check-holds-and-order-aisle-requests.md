# 0023 Check holds before motion and preserve request order

**Status** Accepted for Phase 25

An observed run checks its cancellation predicate before submitting a command and again before returning observations to the job. A hold received after the first check can overlap a tick already being submitted. Workers brake when unwinding. A recorded audit keeps the durability guarantees established in Phase 23.

The fleet registers a reservation when it accepts an audit request. If two overlapping requests arrive in the same render frame, the earliest accepted request gets precedence. Canceling a request before dispatch releases its pending claim. A robot already holding an aisle keeps its claim until berth clearance is confirmed.
