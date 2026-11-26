# ARGUS 0.1.0 Windows x64 preview

ARGUS is an offline C++20 simulator for retail stockroom mapping and probabilistic RFID audits. Two robots audit whole shelves and Wacos in one fictional stockroom. Fleet shows work by location, shared-aisle yielding and holds. Location Audits shows saved DPCIs, photos, quantities, RFID evidence and linked re-audits. Robot Maintenance retains full LiDAR rays, trails and localization details. **Single robot** opens the original driving, mapping and mission workflows.

**Download** `ARGUS-0.1.0-windows-x64.zip` and its SHA-256 sidecar from the same verified release. The ZIP contains the program, app-local runtime, fictional scenarios, notices, build provenance, and passing build tests. The Windows application binaries are unsigned.

**Target environment** Windows 11 x64 with an interactive OpenGL-capable desktop. CLI workflows can run without graphics. The verification report identifies the tested machine and open fresh-machine gate.

**Media** The showcase assets are `ARGUS-0.1.0-fleet.png` and `ARGUS-0.1.0-fleet-walkthrough.mp4`. The recorded walkthrough shows the Fleet dashboard, Robot Maintenance, a held robot returning to berth and the linked re-audit comparison. [Native recording and tour commands](../operations-center.md#local-verification-and-recording)

## Changes and verification

The simulation has no real-world safety certification. RFID is probabilistic and uncalibrated. The preview uses fictional data and runs offline. [Changelog](../../CHANGELOG.md) · [Installation and tutorial](onboarding.md) · [Performance measurements](../testing/phase-15.md) · [Distribution notice](NOTICE.md)

## Two-minute demonstration

| Time | Show |
| --- | --- |
| 0:00–0:20 | Launch the verified portable copy with an operations data directory. Assign Robot 1 to `A10J05` and Robot 2 to `A20J06-W01`. |
| 0:20–0:45 | Open **Robot Maintenance** while a robot is moving. Show the full LiDAR rays, trail and localization details, then return to Fleet. |
| 0:45–1:10 | Open the finished whole-shelf audit. Show the photos, DPCIs and quantity differences. Filter to locations that need review and mark the report reviewed. |
| 1:10–1:35 | Start a re-audit and open the count comparison. Point out the new review status and the original audit in history. |
| 1:35–2:00 | Restart with the same operations directory and reopen the comparison. Show the ZIP checksum and verified package receipt. |
