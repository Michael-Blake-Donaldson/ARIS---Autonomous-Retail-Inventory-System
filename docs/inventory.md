# Inventory catalog and scan evidence

Run from the repository root after building with a standard preset.

```bat
out\build\windows-release\argus-inventory.exe out\inventory.db import assets\inventory fictional-catalog.csv
out\build\windows-release\argus-inventory.exe out\inventory.db find 990-01-0001
```

On Linux, use `out/build/linux-ci/argus-inventory` and forward slashes. The equivalent JSON fixture is `fictional-catalog.json`. Use a fresh database to import the second representation of the same catalog. Successful commands emit one JSON document. A lookup returns the product and up to 256 assigned EPCs in lexical order. Pass the returned `nextAfterEpc` as the last argument to fetch the next page. The cursor is `null` at the end.

## Catalog imports

Use the CSV header `kind,id,value,detail`. Quote fields containing commas or double quotes, and double embedded quotes. LF and CRLF are supported. Each row occupies a single line. JSON uses an object with numeric `version` set to 1 and a `records` array. Each record has four string keys with the same mapping as CSV.

| Kind | id | value | detail |
| --- | --- | --- | --- |
| `product` | DPCI-style ID | UTF-8 product name | `clothing`, `shoes`, `electronics`, or `home-goods` |
| `bin` | Bin ID | Aisle ID | Empty string |
| `unit` | EPC | Product ID | Assigned bin ID |
| `stock` | Product ID | Bin ID | Expected quantity in decimal |

A DPCI uses `DDD-DD-DDDD` with ASCII decimal digits. EPCs use exactly 24 uppercase hexadecimal digits. Bin and aisle IDs start with an uppercase ASCII letter, followed by uppercase letters, digits, or hyphens, up to 32 characters. Product names use up to 128 UTF-8 bytes with control characters excluded. Expected quantity may range from zero through 1,000,000.

Files are limited to 512 KiB and 4,096 total records. CSV lines are limited to 1,024 bytes. JSON limits nesting, parse events, and string widths, and rejects duplicate keys. Rows can reference products and bins in the same batch or already present in the database. Import is additive and atomic. Duplicate identifiers, invalid records, or dangling references reject the batch.

The operator configures the import directory and SQLite path. The import accepts one lowercase ASCII leaf filename of at most 80 characters, using letters, digits, underscores, or hyphens and a `.csv` or `.json` suffix. The loader rejects directories, nonregular files, symlinks, multi-link files, Windows reparse points and device names. It only reads through the opened file handle. Keep the configured directory and its ancestors under operator control. Store the database in a local operator-controlled directory and outside source control.

## Repository behavior

| Call | Behavior |
| --- | --- |
| `Repository::open` | Opens or creates an ARGUS SQLite file and applies pending migrations. Each connection belongs to one caller thread. |
| `insert_catalog`, `update_product`, `remove_product` | Store catalog records and maintain product metadata. Product deletion is restricted while units or expected stock reference it. |
| `find`, `expected_at` | Read assigned EPCs by product or expected quantities by aisle. Pass the last row to `expected_at` to continue from its bin and product. |
| `start_session`, `session` | Create or recover mission, robot, session and start-time metadata. Numeric IDs are positive and timestamps use milliseconds in the session's clock. |
| `record`, `record_rfid` | Append a batch of up to 256 readings. Each stores an EPC, timestamp, estimated pose, antenna, confidence, signal strength in dBm, and up to 256 bytes of optional raw evidence. |
| `observations`, `observed` | Retrieve evidence by session or EPC across sessions. Each reading has an evidence ID that can be used as a continuation cursor. |

The RFID adapter consumes the public simulated scan observation. The source supplies EPC, time, estimated pose and received signal strength. Its evidence uses an empty raw byte payload and zero location confidence because the simulator provides no receiver packet or location confidence. A session may contain repeated readings for one EPC or readings for EPCs absent from the catalog.

Expected stock, assigned EPCs, and observed reads are independent. Observation writes leave catalog and expected counts unchanged. Warehouse truth stays in the simulator.

## Persistence and errors

SQLite schema version 1 contains products, bins, assigned units and expected stock. Version 2 adds missions, scan sessions and evidence. `application_id` identifies ARGUS files. Migrations run in a single write transaction and reject unrelated files and newer schema versions. Migrations are forward-only. Restore a consistent pre-upgrade backup to recover the previous schema.

Catalog and evidence values are bound to prepared statements. Foreign keys are enabled on every connection. Opening a database verifies page boundaries and runs a bounded integrity check. The default main database quota is 256 MiB. C++ callers can choose a smaller quota with `Repository::open(file, byte_limit)`. Exhausted storage or damaged pages return `database_failure`. Write lock contention returns `busy` after a bounded wait and leaves the attempted transaction unapplied. Data errors include `invalid_data`, `too_large`, `duplicate`, `constraint`, and `not_found`. A path outside the import policy returns `invalid_import_path`. Errors are reported by code without echoing rejected values or SQLite diagnostics. The CLI returns 2 for usage errors and malformed lookup identifiers, and 1 for import, persistence and lookup failures.
