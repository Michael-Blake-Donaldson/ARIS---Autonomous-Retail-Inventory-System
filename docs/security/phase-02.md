# Phase 2 scenario import threat model

The app accepts a bounded JSON byte stream or a known compiled fixture name. The scenario parser, simulation state, renderer and later sensor generation are separate interfaces.

| Threat | Control | Verification |
| --- | --- | --- |
| Oversized input, excessive nesting, long strings, or too many nodes | Cap byte reads and parser events before model construction. Limit records and spatial index dimensions. | Large stream, depth and event-budget tests |
| Ambiguous or malformed JSON | Reject duplicate decoded keys, NUL bytes, invalid UTF-8, trailing content, overflow and non-integer coordinate tokens. | Parser tests and a Windows Ctrl-Z suffix regression |
| Files outside an intended directory | Use standard input for import and an allowlist for fixture selection. The application never interprets scenario text or a fixture label as a path. | CLI traversal rejection and byte-for-byte fixture tests |
| Unsafe geometry or dangling references | Validate bounds, cardinality, unique IDs, shelves, exact references, spawn and actor sweeps before creating a world. | Boundary, overlap, replay and reference tests |
| Hidden placements in logs or preview output | Use typed error codes, serialize allowlisted preview fields and grant item queries only to the simulation-side sensor adapter. A deliberate fixture export contains authored fictional items. | Type checks, regional sensor tests and headless preview output checks |

An input producer controls delivery time until EOF or the byte limit. The desktop import completes before creating an SDL window. Keep scenario fixtures fictional. Review sensor data boundaries again when Phase 4 introduces measurements.
