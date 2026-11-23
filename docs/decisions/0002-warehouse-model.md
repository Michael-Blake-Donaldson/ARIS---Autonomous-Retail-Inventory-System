# Define deterministic warehouse simulation and import boundaries

**Status** Accepted

## Context

Phase 2 needs a reproducible floor plan and actor replay. The world must validate imported layouts and make warehouse geometry available to rendering and future sensor generation while protecting hidden inventory positions from operational interfaces.

## Decision

Represent authored geometry in integer millimeters and advance actors at absolute integer ticks. Use a fixed 4 m grid with bounded masks for static candidates and an exact closest-point disk test after candidate lookup. Scan the bounded dynamic actor list for each collision query. Validate actor paths using conservative swept rectangles. This guarantees clear spawn and mutually disjoint actor paths for every seed accepted by the validator.

Decode bounded streams with nlohmann-json 3.12.0#2 from the existing vcpkg baseline. Validate parser events for depth, count, text size and duplicate keys, then validate schema and geometry before creating immutable state. Command-line imports read standard input and perform no path selection. Source-check the three built-in fixtures and export their original bytes by explicit name. [nlohmann JSON parser callbacks](https://json.nlohmann.me/api/basic_json/parser_callback_t/)

Keep renderer data in an owned snapshot and item truth in the simulation-side sensor query capability. Place build orchestration and CLI handling in the existing application layer.

## Alternatives

Floating-point geometry would require another numeric reproducibility contract. An all-object scan alone would make each static query inspect every obstacle. General pathname imports would need a file-opening trust model and race-safe path checks.

## Consequences

Integer coordinates have millimeter resolution. Dynamic paths are straight and axis-aligned. Conservative path validation can reject paths that intersect geometrically at different times. The desktop draws a diagnostic floor plan with OpenGL. Robot locomotion and full 3D rendering have later phase gates.
