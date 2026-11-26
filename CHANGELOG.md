# Changelog

## 0.1.0

- Introduced the Windows operations center and headless CLI for fictional warehouse scenarios, drive controls, LiDAR mapping, and autonomous exploration.
- Added catalog lookup, RFID item investigation, aisle audit, review exports, mission checkpointing, and recovery.
- Added atomic version 2 map export with SHA-256 verification, bounded SQLite and report processing, and stress-tested simulation of up to 10,000 tags.
- Added an unsigned Windows x64 portable package with fictional examples, dependency notices, verification logs, and checksums.
- Added live LiDAR age, localization bounds, and navigation decisions, with a simulated dropout control that demonstrates hold and recovery.
- Connected exploration, RFID investigations, audits, and routed missions to one simulated robot. The desktop can save its map and audit and reopen them after a restart.
- Reused occupancy vertices across frames sharing an observed map revision, with separate simulation and rendering benchmarks.
- Added a two-robot fleet in a shared fictional stockroom, with aisle yielding and an operator-controlled return to berth.
- Added whole-shelf and Waco histories, product photos and DPCIs, discrepancy review, and linked re-audits with a side-by-side count comparison.
- Extended portable verification to check maintenance, aisle recovery, and review history after replacing the program.
