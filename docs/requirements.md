# ARGUS requirements

ARGUS is an offline Windows simulator for retail stockroom mapping, RFID inventory audits, and robot mission control. The desktop operator works with fictional inventory and a simulated robot. The engineer uses headless runs and automated tests to reproduce behavior. The reviewer examines build and test evidence before advancing a phase.

## Phase 1 use cases

| Actor | Use case | Acceptance |
| --- | --- | --- |
| Operator | Open the desktop shell | A native window opens, responds to close or Escape, and reports initialization failures as a nonzero exit. |
| Engineer | Run the headless executable | Both executable entry points provide help and version information. Headless runs with the same seed emit the same first RNG sample. |
| Engineer | Pass invalid configuration | Unknown, malformed, out-of-range, or extra arguments fail before application startup, with a stable error code and nonzero exit status. |
| Engineer | Rebuild from a clean checkout | Windows x64 Debug and Release and Linux x64 verification have documented presets. vcpkg resolves the dependency graph from a committed baseline. |
| Reviewer | Verify quality gates | GoogleTest and executable smoke tests cover startup and core behavior. CI runs builds, tests, static analysis, formatting, and Linux sanitizers. |
| Reviewer | Verify boundaries | The desktop and headless applications depend inward on shared startup and core libraries. Core has no platform, logger, or application dependencies. Random state is owned by the caller. |

This phase defines a scenario schema example. Warehouse geometry, scenario ingestion, robot behavior, sensors, inventory, missions and the 3D operations center belong to later phases. Physical robot control, retailer integration, real inventory or customer data, and networked deployment are outside the first release.

## Phase 13 simulation showcase extension

| Actor | Use case | Acceptance |
| --- | --- | --- |
| Operator | Inspect the live map | Show accepted LiDAR scan age, distinguish fresh, stale and unavailable scans, and hide expired rays while preserving the accumulated occupancy map. |
| Operator | Interpret localization and navigation | Show mapper reliability and error bounds, mark uncertain poses visibly, and explain the navigator's current hold or yield, replan count and latest replan cause. |
| Reviewer | Demonstrate sensor dropout | Withhold LiDAR observations through the native map controls. The tour must observe a stale scan, a navigator hold with zero commanded translation, and fresh scan recovery. |
| Engineer | Protect the simulation boundary | Derive status from public telemetry, observations and estimates. Keep simulator truth limited to the existing explicit diagnostics and evaluation paths. |
