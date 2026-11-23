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
