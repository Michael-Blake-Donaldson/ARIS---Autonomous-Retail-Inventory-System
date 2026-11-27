# 0024 Reuse backing buffers for fleet snapshots

**Status** Accepted for Phase 26

The worker creates a preview snapshot for the desktop while retaining independent ownership of the live map. The initial profile showed frequent occupancy copies. Sharing a grid by revision alone saved almost no copies because the mapper frequently publishes a new revision.

Keep a bounded pool of three grid buffers per workbench. Reuse the current snapshot for an identical run, revision, source and specification. For a different revision, the worker can update a pool buffer while the pool is its only strong owner. Readers hold strong immutable snapshots. If every pool buffer is referenced, publish an independent grid and keep the pool bounded. The mapper's live grid remains the data source.

Desktop metrics count backing allocations separately from copies and are enabled only for scripted measurements. The benchmark compares fixed-size populations with the same stockroom fixture and report limits. It records renderer and rendezvous latencies alongside snapshot data so the allocation count cannot stand in for end-to-end responsiveness.
