# RFID item search and aisle audit

Load the fictional catalog once, then choose a fresh positive session ID for each investigation.

```bat
out\build\windows-release\argus-inventory.exe out\inventory.db import assets\inventory fictional-catalog.csv
out\build\windows-release\argus-inventory.exe out\inventory.db find-item 990-01-0001 801 SOFTLINES
out\build\windows-release\argus-inventory.exe out\inventory.db audit SOFTLINES 802 json
out\build\windows-release\argus-inventory.exe out\inventory.db audit SOFTLINES 803 csv > out\review.csv
```

Use `out/build/linux-ci/argus-inventory` on Linux. `find-item` accepts an optional last-known aisle. `audit` accepts `json` or `csv` and defaults to JSON. Both commands run the seeded `rfid-audit` warehouse with configured viewpoints for `SOFTLINES` and `TECH`. The original `find` command performs a catalog lookup without a robot mission.

## Search and observation

Find Item gets the assigned EPCs for a fictional DPCI. The first pass orders aisles by the optional last-known hint, then by catalog assignments. A second pass uses unresolved EPCs and candidate aisles from the first pass to select alternate viewpoints. Each selected viewpoint gives the navigator a position and heading. Known-free routing, LiDAR clearance, motion caps, return reserve, and the localization stop conditions still apply.

A viewpoint normally collects 14 RFID frames. At least eight fresh, correctly aimed, untruncated frames qualify for coverage. Every read contributes to the session's raw evidence. The report counts each EPC once and keeps its first and last timestamps, read count, and strongest estimated antenna footprint per view. The SQLite session preserves individual EPCs, times, estimated poses and signal levels. The footprint origin represents the receiver position.

Aisle attribution is a heuristic, using overlap between configured aisle regions and estimated antenna footprints, signal strength, and odometry uncertainty. At least two independent supporting views and a score of at least 0.58 are required for a supported aisle. The score is an ordering aid with no probability calibration.

## Interpreting and reviewing findings

| Finding | Meaning |
| --- | --- |
| `observed_in_assigned_aisle` | Multiple distinct views support the assigned aisle. |
| `likely_misplaced` | Multiple distinct views support an aisle other than the assignment. |
| `ambiguous_aisle` | An EPC was observed with insufficient separation between candidate aisles or viewpoints. |
| `unscanned` | No qualified view reached the aisle. |
| `insufficient_coverage` | One qualified view produced no matching read. |
| `not_observed_after_two_views` | Both qualified views produced no matching read. |
| `unknown_epc` | An observation has no catalog assignment. |

The JSON audit separates expected quantities, assigned EPC counts, and unique EPCs with attribution to the selected aisle. A discrepancy is flagged for review. Coverage and location describe evidence quality. Confirming an item as missing requires independent verification.

CSV contains pending EPC exceptions. Use the session-and-EPC case ID to cross-reference the JSON report and stored readings. Record the independent verification result and notes in the CSV's review columns. The imported catalog and expected quantities stay unchanged, and the exported worksheet is a separate review record. CSV fields are quoted, control characters are sanitized, and leading spreadsheet formula markers are neutralized. JSON escapes quotes, control characters and non-ASCII text.

Search and audit are limited to the configured fixture. A visit can be unreachable, lose its localization estimate, or hit a step limit. Failed views leave their coverage unqualified. The mission stops after at most 1,800 ticks and caps accepted data at 16,384 reads and 1,024 distinct EPCs. An inventory or input error returns a structured error code. Previously committed scan evidence remains queryable by session.
