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

## Phase 14 continuous simulation workflow

| Actor | Use case | Acceptance |
| --- | --- | --- |
| Operator | Follow the robot from mapping through routed work | The bundled RFID audit stockroom keeps one robot, mapper, sensor RNG and run identity across exploration, search, audit, scan and dock jobs. |
| Operator | Follow a job and retain control | The dashboard renders observed progress during routed work. Hold, focus loss and emergency stop interrupt worker commands at a simulation step boundary. |
| Reviewer | Save and inspect the run after reopening | Save a verified occupancy snapshot and audit report with their run and session identifiers. Reopen the evidence in a fresh desktop process without advancing the saved run. |
| Engineer | Preserve existing data and isolation | Migrate existing mission journals, bind storage values, bound report and map inputs, and keep simulator truth inaccessible to the worker's progress snapshots. |

## Phase 15 performance profiling

| Actor | Use case | Acceptance |
| --- | --- | --- |
| Engineer | Measure simulation and rendering independently | Record headless step latency at representative catalog sizes and native frame and scene latency. Report p95 and p99, allocation measurements and cache-miss evidence with the counter source and limits. |
| Operator | Render a map across repeated frames | Reuse occupancy geometry across frames that share an observed map revision. Refresh it for new observations, resets, run changes and archived evidence. |
| Reviewer | Verify that the optimization preserves results | Compare seeded headless outputs and cached versus uncached geometry. Exercise the continuous desktop workflow, saved-run review and graphics recovery. |

## Phase 16 simulation showcase acceptance

| Actor | Use case | Acceptance |
| --- | --- | --- |
| Operator | Follow a single stockroom run | Show observed mapping, a LiDAR dropout hold and recovery, product investigation, aisle audit, routed missions and saved-run review. |
| Reviewer | Reproduce the handoff from the portable package | Verify the ZIP and test receipt. Run the native tour from the extraction, replace the program, reopen the original desktop evidence in a fresh process and confirm removal preserves the data. |
| Reviewer | Inspect the demonstration and its limits | Provide a native recording and walkthrough, link to measured performance and disclose simulation, signing and host verification limits. |

## Phase 17 fleet and location audits

| Actor | Use case | Acceptance |
| --- | --- | --- |
| Operator | Inspect two simulated robots | Show each robot's task, battery and status. Selecting a robot highlights its observed route and assigned location. |
| Maintainer | Inspect a robot during motion | Use a dedicated maintenance screen with the original full-brightness LiDAR rays and motion trail, scan age, localization state and navigation hold reason. |
| Operator | Track labeled shelves | Show aisle and shelf identifiers such as A10J05. Display pending, in-progress and complete in red, amber and green with text. Show discrepancy status independently. |
| Reviewer | Read every product on a finished audit | Open a completed shelf to see its saved product list, photos, DPCIs, names, expected and observed quantities, and expandable EPCs. Include expected stock with zero reads and provide a discrepancy filter. |
| Reviewer | Exercise durable independent runs | Start audits with two robots against distinct shelves, observe both progress, then restart and reopen each report with its robot identity and Product details. |

## Phase 18 operator dashboard polish

| Actor | Use case | Acceptance |
| --- | --- | --- |
| Operator | Track the fleet on one map | Show both observed robot estimates, selectable shelf labels and the selected robot's route on one warehouse map. Show aisle labels at the overview scale and shelf IDs when zoomed in. |
| Operator | Review a completed shelf quickly | Open a completed shelf's audit from Fleet in at most two clicks. Preserve the selected robot and shelf across Fleet, Location Audits and Robot Maintenance. |
| Operator | Find products and discrepancies | Search shelf IDs, open a completed audit, and compare organized product photos, names, DPCIs and quantities. Expand RFID tags and earlier audits on demand. |
| Maintainer | Identify a hold and investigate | Surface the hold and its reason in Fleet. Keep the selected robot's original LiDAR rays and motion trail, scan age and localization status in a dedicated maintenance view. |
| Reviewer | Verify the compact desktop layout | At 1280 × 720 all three views fit their data without horizontal scrolling. Exercise the native workflow without disturbing existing single-robot entry points. |

## Phase 19 mixed backroom locations

| Actor | Use case | Acceptance |
| --- | --- | --- |
| Team member or lead | See progress at a glance | Fleet summarizes completion, pending work and review by aisle. A divided shelf contributes its Wacos to the totals. |
| Operator | Audit a specific location | The showcase has four aisles, three shelf levels per aisle and a ten-Waco shelf. Whole shelves and individual Wacos have distinct statuses and histories. |
| Reviewer | Verify local RFID attribution | Attribute a tag only with independent observations that distinguish the target location. Keep unresolved reads available for review and exclude them from observed counts. |
| Maintainer | Inspect a selected robot | Keep the full LiDAR rays, motion trail, localization and navigation details in Robot Maintenance. |
| Reviewer | Check restart and compatibility | Reopen whole-shelf and Waco reports in a new process. Preserve previous histories, original single-robot workflows and default seeded sensor behavior. |

## Phase 20 shared fleet simulation

| Actor | Use case | Acceptance |
| --- | --- | --- |
| Operator | Run both robots in one stockroom | Both robots advance on one fixed simulation clock, see the same moving actors and can detect each other through the simulated sensors. |
| Operator | Coordinate narrow-aisle work | Assign competing work, see which robot is yielding, and verify that both jobs can proceed in order. Stop commands release or retain access according to whether the robot has cleared the corridor. |
| Reviewer | Verify collision behavior and provenance | Test concurrent motion, physical separation, moving obstructions, cancellation, and completion of both assigned audits. Keep true positions inside physics, sensor generation and explicit test evaluators. |

## Phase 21 discrepancy review and re-audits

| Actor | Use case | Acceptance |
| --- | --- | --- |
| Team lead | Find work that needs review | Fleet counts locations whose latest report has unreviewed discrepancies or uncertain RFID evidence. Reviewing a report updates that count and leaves measured quantities and evidence intact. |
| Operator | Recheck a location | From a completed location, queue a fresh audit with the selected available robot. Completing it creates a separate report linked to its source and starts a fresh review when warranted. |
| Reviewer | Compare the evidence | Compare expected and observed counts by DPCI against the source audit. Keep unresolved and unidentified tags inspectable on each report. Preserve robot, run and session provenance and separate historical evidence from current review status. |
| Reviewer | Verify durability | Migrate version 4 journals without rewriting reports. Reopen review timestamps and re-audit links after restart. Reject invalid or stale actions and preserve both existing history limits and single-robot behavior. |
