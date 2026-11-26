# Phase 25 principal engineering review

The review follows the main data and control paths from launch and input validation through motion, evidence, storage, rendering and release. It covers source inspection, existing test evidence and targeted regressions. The hardware scope is simulation.

## Areas reviewed

| Area and entry points | Evidence and limits |
| --- | --- |
| Core, scenario and physics | Reviewed typed IDs and units, seeded random ownership, bounded scenario decoding, `RobotSimulation::prepare` and paired commit. Geometry, dynamic obstruction and seeded reference tests provide reproducibility. |
| Sensors and mapping | Reviewed queue capacity, sample sequence handling, RFID truncation, mapper uncertainty, map import limits, digest verification and file replacement. Test evidence uses simulated sensor outputs and independently checked map fixtures. |
| Navigation and traffic | Reviewed planner inflation, stale scans, reserve energy, cross-aisle waypoints, admission, relinquishment and the shared tick deadline. Identified the motion-hold and request-order defects below. |
| Inventory and auditing | Reviewed catalog constraints and bounded imports, duplicate RFID handling, the two-view shelf attribution rule and report compatibility. Adjacent tags that cannot be isolated remain unresolved. Coverage accuracy across varied layouts and tag densities has limited measured evidence. |
| Mission and audit persistence | Reviewed SQLite ownership, transaction rollback, lease revisions, schema migration, history quotas and immutable evidence with review and re-audit references. Tests cover concurrent checkpoint/cancellation, revised-schema reopening, locked writers and capacity rollback. |
| Desktop and CLI | Reviewed event handling, the worker mailbox, observed-frame ownership, maintenance separation, option parsing and native CLI entry points. Identified the Escape defect below. |
| Build, dependencies, test and release | Reviewed the target graph, pinned registry, CI definitions, native suites, Linux sanitizer targets, package content checks and digest receipts. The tested release estate is one Windows host and WSL. A second clean Windows installation remains a release gate. |

## Findings register

| Severity | Finding and user impact | Disposition |
| --- | --- | --- |
| High | Routed work advanced motion before checking the worker's interruption flag. A hold already requested before a new tick could still allow that tick to move the robot. | Check the cancellation predicate before submitting motion and again before releasing observations to the caller. A regression asserts that an already requested hold leaves the sample tick unchanged. |
| Medium | Fleet built aisle reservations in robot index order while draining queued requests. Robot 1 could take priority when Robot 2's request arrived first in the same frame. | Register reservations when the request is accepted. A hold removes queued reservations that have yet to dispatch. Native probes queue both directions and verify visible yielding and completion. |
| Medium | Escape closed the application while an operator was editing the location search. | Give the active text edit Escape first and ignore repeated Escape key events for window exit. Native review clicks the search, cancels it, checks the selected report and continues through the review workflow. |
| Medium | The architecture guide described an older ownership model and listed persisted review as future work. | Updated the guide to reflect worker ownership, the shared clock and audit provenance. |
| Medium | A peer timeout closes the rendezvous, while a worker blocked inside an OS operation can still delay process shutdown. | Open. The timeout regression verifies no partial tick. A process-level design or cancellable I/O would need a fault demonstrator and shutdown timing evidence before changing ownership. |
| Medium | Fleet traffic and scan viewpoints are tightly coupled to the authored stockroom. Audit accuracy from the one fixture provides limited evidence for layout and density variation. | Open. Compare layout-aware reservation and accuracy approaches below. |
| Release gate | The portable package still needs native acceptance on an independent clean Windows installation. | Open. The packaged verification script and receipt procedure are ready. |

## Ranked improvement backlog

| Priority | Option and value | Evidence needed |
| --- | --- | --- |
| 1 | Measure fleet frame, tick and input latency before UI changes. The renderer currently copies an observed grid when publishing a preview, which is safe ownership but potentially expensive. Compare the current path with an immutable revision-shared grid only if copying shows up in profiles. | Matched two-robot workloads with contention, Maintenance and a long report. Capture p95/p99, allocations and evidence parity. Phase 26 owns this measurement. |
| 2 | Expand simulated attribution coverage. Use generated shelving arrangements and tag distributions to check Waco attribution, missed reads and false cross-aisle matches across seeds. A larger synthetic backroom is a useful stress workload. | Ground truth stays accessible to an explicit evaluator. Publish per-location confirmed, unresolved and misattributed rates with repeatable inputs and bounded runtime. |
| 3 | Compare route-aware traffic designs. The current one-dimensional reservation is simple and conservative for the authored stockroom. A graph of shared aisle blocks offers clearer conflict semantics than loose route rectangles, while requiring explicit reserve-before-replan behavior. | Prove interleaving, clearance retention and bounded recovery on intersecting routes and multiple layouts before replacing the current policy. |
| 4 | Reduce the desktop entry point's test-harness load and investigate shutdown escalation. Splitting scripted probes from the interactive loop improves reviewability. A separate process for workers would improve fault isolation at a larger cost. | Preserve CLI and package probes during extraction. Isolate a reproducible blocked-operation case before changing the worker model. |
| 5 | Close the clean-host gate and evaluate signing for public distribution. | Run the included script on a separate Windows installation with an interactive graphics driver, record the host and verify the archive and receipt. |
