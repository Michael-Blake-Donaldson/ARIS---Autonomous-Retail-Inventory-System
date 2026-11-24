# Phase 5 observation and snapshot boundary

| Failure mode | Control | Verification |
| --- | --- | --- |
| Stale or mismatched sensor input | Check sensor identity, sample time, monotonic odometry and scan association. Reject malformed scans before updating evidence. | Replay, metadata and malformed-reading tests |
| Non-finite coordinates or out-of-frame beams | Validate ranges and pose. Clip rays, bound the grid and stop traversal at valid indices. | Coordinate round-trip, clipping and saturation tests |
| Corrupt or incompatible snapshot | Validate exact length, version, frame, scenario, resolution, legal evidence and checksum before publishing a loaded grid. | Save/reload, checksum, length and metadata mismatch tests |
| Unintended filesystem access | Operate on standard input and output for CLI snapshots. Application code receives bytes. | Process integration tests with redirected streams |
| Ground truth in mapping logic | Consume public observations and require an explicit external pose for re-anchoring. Keep truth-based metrics in a test-only executable. | Dependency and map output review |

The file checksum detects accidental corruption. Treat externally supplied evidence as untrusted even when its checksum matches. A stream producer must close the input to finish loading.
