# 0007 Keep expected stock and RFID evidence in separate relational tables

**Status** Accepted for Phase 7

## Context

Inventory records need to survive restart and preserve the difference between expected quantities, assigned tagged units, and detections from a robot mission. Operator imports can contain malformed records, duplicate identities, invalid references, and hostile paths.

## Decision

Use the pinned SQLite port with a normalized, strict schema, foreign keys, indexed lookups, and prepared statements. Products and bins define the catalog. Expected quantities and assigned EPCs reference those records. Evidence references a scan session and retains the observed EPC independently of catalog membership. Each catalog import, evidence batch, or schema upgrade commits atomically.

The repository owns one connection per caller thread. Schema version 1 is the catalog baseline and version 2 adds mission and scan data. Track the schema with SQLite's application and user versions. Keep migrations forward-only so a downgrade cannot silently discard observations.

Resolve imports through an opened trusted directory and reject path syntax and links at the file boundary. Validate both CSV and JSON before constructing database statements. The inventory target depends on core types and public sensor observations. Only the test adapter setup uses the simulator and no truth input reaches inventory.

## Consequences

Duplicate catalog imports require a different database or a distinct set of records. An observed EPC may have no assigned product. Historical observations survive future catalog changes. Retrying a failed atomic batch is safe after resolving its reported failure. Restoring an older database format requires a backup.
