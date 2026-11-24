# Phase 11 local desktop threat model

## Trust boundaries

An operator can supply scenario JSON, fictional inventory CSV or JSON, and occupancy snapshots. Treat the bytes as untrusted even when received from an authenticated source. The application uses selected local SQLite paths and operator-owned import and export directories. State must be kept on a local filesystem with working locking and atomic rename. An attacker who can write the application binary, trusted digest, database directory, or its ancestors can compromise the local run. The application has no network listener or remote input path.

| Boundary and failure | Controls | Remaining exposure |
| --- | --- | --- |
| Scenario and catalog parsers | Scenario input has a 2 MiB limit, a depth and parse-event budget, validated IDs and geometry, and at most 10,000 item placements. Catalogs allow 512 KiB and 4,096 records. Both reject raw NUL. CSV quoting, field counts, UTF-8, and duplicate keys are validated. | Valid inputs near the caps cost more CPU and memory. A local input stream must terminate. |
| Files, links and assets | Catalog inputs are read from an anchored operator directory, require allowlisted leaves and ordinary single-link files, and enforce a byte cap. Map output uses an exclusively created same-directory temporary file. Desktop graphics use compiled font data and procedural meshes. | Operator-controlled directories and their ancestors are required. User-supplied images, fonts, model decoders and scripts are outside the application interface. |
| SQLite data and failed writes | Database paths exclude URI and memory forms and SQLite opens with its no-follow flag. Bound SQL values, defensive mode, foreign keys, main-file quotas, transaction rollback and a bounded quick check apply. Views, triggers, virtual tables and shadow tables are refused during open. | Detection does not repair corruption or establish provenance. An application ID is a format marker. Use application-managed databases and trusted backups. Journal and temporary file space is additional. |
| Snapshot tampering and interruption | Version 2 verifies SHA-256 plus exact frame, scenario, cell dimensions, length and legal cell values. A separately trusted digest can pin an import. File export flushes before same-directory atomic replacement. | The embedded digest does not authenticate its source. Failed storage after rename may leave publication visible with uncertain durability. A process killed before rename can leave a temporary file. |
| Resource exhaustion, stale evidence and secret leakage | Map grids, sensor rays and queues, scan and mission limits, and exported report structure have independent caps. The renderer uses clipped and bounded batches. Cancellation advances a durable revision and invalidates outstanding leases. Fixtures and evidence use fictional inventory. | A large scene can truncate RFID reads at the sensor cap. The application reports this condition and audit scope remains separately bounded. |
| Dependencies and automation | Use an exact vcpkg registry commit, pinned GitHub Actions, bounded fuzzing in Linux CI, and read-only CI permissions. Verify generated per-port SPDX documents and dependency notices at release. | Platform and driver runtime dependencies need their own release inventory. Keep the pinned dependencies reviewed as upstream advisories change. |

SQLite's advisory documentation identifies externally supplied SQL and hostile databases as distinct input paths. ARGUS uses bound SQL and operator-managed SQLite files. For portability it retains the pinned 3.53.4 port and rejects unexpected schema programs when opening state. Review whether a later compatible port should replace this baseline when cutting a release. [SQLite vulnerability and mitigation notes](https://www.sqlite.org/cves.html)

## Local operation

Database open reports a storage error for unreadable or inconsistent state, unsupported schema extensions, work beyond the verification budget, or files beyond their configured quota. Each connection uses a 256 MiB main-file cap by default. Inventory embedders can select a smaller byte limit with the `Repository::open` overload. The quota is rounded down to a main-database page boundary. If a local file is rejected, preserve the database and related journal files together before restoring a verified backup.

Keep map output under an operator-owned directory. A crash can leave a file named `.argus-map-*.tmp`. After confirming that no export is active, the operator can remove those temporary files. The final snapshot name becomes visible after a successful replacement.

For quantified evidence and fault cases, see the [Phase 11 verification report](../testing/phase-11.md).
