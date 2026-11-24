# 0011 Verify snapshots and bound local storage

**Status** Accepted for Phase 11

## Context

Operators can import scenario, catalog, and snapshot bytes. Inventory and mission stores are local application state. Abrupt shutdown, a full volume, or a damaged file can leave data unavailable. A snapshot digest supplied with the snapshot can detect corruption, while the operator may also have a digest received independently.

## Decision

Publish new occupancy snapshots as version 2 with SHA-256 covering the fixed header and evidence. Keep version 1 reads for existing snapshots. Require version 2 when an operator supplies an independent digest. Keep stream output for pipelines and provide an atomic file operation for an operator-owned directory on Windows and Linux.

Use one database connection per owner. Reject over-quota files, partial main pages, unexpected views and executable schema objects before domain queries. Check the database with a bounded SQLite quick check during open. Keep parameter binding, defensive mode and transactional migrations. Configure a maximum main database size for each connection.

Keep the higher simulation tag count independent of the audit evidence and reporting limits. Scenarios retain byte, shape, depth and parser event limits. Audit exports validate their structure and encode one item at a time.

Visit matching tags by reference inside sensor generation and retain the dwell entries that remain in view. Keep scenario order and the simulated scan RNG sequence. Continue clearing dwell on gaps and power loss.

## Consequences

A snapshot's embedded digest is recomputable by anyone who changes the contents. A trusted pin provides integrity against a known export. Legacy snapshots remain suitable for trusted local workflows. A map publication error after a successful rename on Linux can indicate uncertain durability.

The application can reject damaged state without attempting recovery. Operators preserve the original files and use a known-good backup. The quota covers the main database. Free space is still required for journals and temporary files.
