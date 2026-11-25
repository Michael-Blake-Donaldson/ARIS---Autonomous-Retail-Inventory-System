# ARGUS 0.1.0 Windows x64 preview

ARGUS is an offline C++20 simulator for retail stockroom mapping and probabilistic RFID audits. The preview includes manual driving, LiDAR and occupancy views, autonomous exploration, product search, aisle audits, mission history, and portable exports.

**Download** `ARGUS-0.1.0-windows-x64.zip` and its SHA-256 sidecar from the same verified release. The ZIP contains the program, app-local runtime, fictional scenarios, notices, build provenance, and passing build tests. The Windows application binaries are unsigned.

**Target environment** Windows 11 x64 with an interactive OpenGL-capable desktop. CLI workflows can run without graphics. The verification report identifies the tested machine and open fresh-machine gate.

**Media** The prepared release assets are `ARGUS-0.1.0-operations-center.png` and `ARGUS-0.1.0-workflow-tour.mp4`. The clip shows the native simulation window and scripted operations. The walkthrough below combines the desktop and CLI for the complete workflow.

![Inventory audit in the ARGUS operations center](../testing/evidence/phase-10-operations-center.png)

## Changes and verification

Read the [changelog](../../CHANGELOG.md), [installation and tutorial](onboarding.md), [Phase 12 verification](../testing/phase-12.md), and [distribution notice](NOTICE.md). The simulation has no real-world safety certification. RFID is probabilistic and uncalibrated. The preview uses fictional data and has no proprietary retailer connection.

## Two-minute demonstration

| Time | Show |
| --- | --- |
| 0:00–0:15 | Open a verified portable copy and show the fictional warehouse and zero-start live run. |
| 0:15–0:35 | Deploy with the deadman control. Show LiDAR and occupancy, then start Explore. |
| 0:35–0:50 | Show the controlled moving-obstacle scene and explain the observation-driven stop. Briefly toggle the labeled Diagnostics truth overlay and return to the observed view. |
| 0:50–1:10 | Enter `990-01-0001`, show assigned EPCs, and run the item investigation. |
| 1:10–1:35 | Run the `SOFTLINES` audit. Compare expected stock with unique attributed EPCs and open a likely-misplaced review row. |
| 1:35–1:55 | Open the JSON and CSV exports, reload the verified map, and reopen the saved mission report. |
| 1:55–2:00 | Show the build evidence and disclose the simulation and RFID limits. |
