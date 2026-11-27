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

## Phase 22 final simulation release

| Actor | Use case | Acceptance |
| --- | --- | --- |
| Team member or lead | Run the stockroom walkthrough | Launch the portable desktop with a separate operations directory and follow current Fleet, Location Audits, review and Maintenance instructions. Verify the workflow at 1280 × 720 and capture updated preview media. |
| Maintainer | Recover a held robot | Interrupt active work outside the berth, verify the visible hold and retained claim, and use Return to berth to release it. Verify the shared-aisle scenario that previously timed out. |
| Release reviewer | Verify the staged archive | Verify the digest and extract with a system-only PATH. Exercise fleet recovery, review and traffic in the archive and verify the reports and timestamps after replacing the program. |
| Release reviewer | Verify a fresh Windows installation | Run native acceptance on a clean Windows 11 x64 installation with an interactive OpenGL driver and record the host and driver. Record the gate as open if a separate installation is unavailable. |

## Phase 23 fleet failure recovery

| Actor | Use case | Acceptance |
| --- | --- | --- |
| Team lead | Preserve a completed survey | Save completed coverage before driving back. Interrupt the return and verify the completed report, source link and retained aisle claim independently. |
| Maintainer | Diagnose a stalled peer | Stop a stalled fleet rendezvous within a bounded wait. Reject late submissions and preserve both robots' last committed ticks. Keep audit history accessible and mark motion unavailable. |
| Reviewer | Exercise normal and failed runs | Verify the real saved-report interruption path, the missed tick and late peer path, and the existing narrow-aisle and recovery workflows. |

## Phase 24 fault handling and recovery verification

| Actor | Use case | Acceptance |
| --- | --- | --- |
| Operator | Recover from a small mistake | Exercise mistyped or empty location and product IDs, duplicate clicks, stale selections, attempts to act on a busy or held robot, a wrong data directory, canceled dialogs, and focus loss during an action. Keep valid inputs and the selected report where practical. Explain what happened and give the next available action. |
| Operator | Open existing data safely | Exercise malformed, oversized, unsupported and truncated imports, damaged or outdated journals, inaccessible paths, an occupied database and exhausted disk space. Use disposable fixtures. Reject unreadable evidence explicitly and preserve previously committed data. Verify usable workflows remain available after the failure. |
| Maintainer | Recover from interrupted work | Inject worker timeouts, stale sensors, blocked routes, graphics loss, slow storage and failures during writes. Restart at meaningful boundaries before and after a durable commit. Verify safe motion state, consistent aisle reservations, and report provenance after reopening. |
| Reviewer | Verify compound and unlikely faults | Build a matrix for validation, storage, concurrency, sensors, rendering, startup and shutdown. Include credible combinations such as cancellation during a save, focus loss during aisle clearance and a storage failure during re-audit. Record the trigger, expected state, visible feedback, recovery and observed result. |
| Engineer | Prove the recovery path | Use deterministic fault injection where possible. Verify bounded waits and retries, no duplicate audit history, readable error messages and successful retry or restart. Add targeted regressions for fixes and exercise representative faults through the desktop, CLI and extracted package where applicable. |

The matrix prioritizes impact on data integrity, motion and operator recovery as well as likelihood. Verify that an ordinary action following each recoverable fault succeeds. Record unexercised cases and their blockers in the phase report.

## Phase 25 principal engineering review and improvements

| Actor | Use case | Acceptance |
| --- | --- | --- |
| Principal reviewer | Review the entire program | Inventory each subsystem, its interfaces and its entry points. Review core C++ ownership and lifetimes, simulation and sensor fidelity, navigation and traffic, inventory, persistence, desktop and CLI, build and dependencies, test coverage and release tooling. Record reviewed areas and findings with evidence, severity and user impact. |
| Engineer | Correct verified defects | Prioritize data loss, invalid motion state, races, stalls and incorrect results. Fix confirmed issues in the phase scope, cover each fix with applicable regression evidence and verify affected workflows across the supported builds. Record the disposition and impact of every finding. |
| Principal reviewer | Identify improvements with measurable value | Evaluate layout-aware traffic reservations, audit accuracy across varied scenarios, fleet performance under contention, maintainability, operator recovery and deployment. Compare plausible approaches using cost, benefit and evidence. Implement improvements justified by the review and rank deferred work with the evidence it would need to proceed. |
| Release reviewer | Verify the handoff independently | Re-run relevant native and headless workflows, seeded parity where behavior should match, migrations, static analysis, applicable sanitizers and extracted-package checks. Keep any unverified platform or fresh-install gate explicit. Require evidence for every closed critical or high finding. |

The review produces a findings register and an improvement backlog. Record and prioritize lower-severity defects and design tradeoffs so they remain visible after the phase.

## Phase 26 performance-first desktop usability and visual refinement

| Actor | Use case | Acceptance |
| --- | --- | --- |
| Team member or lead | Read status and act quickly | Make Fleet's at-a-glance states clear for pending, running, complete, held and needs review. Verify the shortest paths to open a finished shelf or Waco, inspect product evidence and queue a re-audit. Use text alongside color and keep task selection visible across views. |
| Maintainer | Inspect a moving robot | Verify full-brightness LiDAR, motion trails, scan age, localization and hold details in Robot Maintenance. Keep diagnostics easy to reach from Fleet and verify a return to the current location report without losing context. |
| Operator | Use the desktop comfortably | Refine typography, spacing and hierarchy. Verify keyboard navigation and focus, legible status contrast, and readable empty, error and hold states. Exercise the 1280 × 720 layout, larger windows and high-DPI settings, with representative mixed shelves and long reports. |
| Performance reviewer | Compare before and after | Establish a baseline on the same hardware, build mode, packaged assets, display size, scenarios and seeds. Take repeated measurements of frame p95 and p99, simulation tick p95 and p99, input-to-visible feedback, allocation and peak memory. Cover idle Fleet, two active robots on separate routes, aisle contention, Maintenance and a long audit report. |
| Engineer | Ship changes that earn their cost | Profile rendering and snapshot work to find actual hot spots. Compare candidates against the baseline and its observed variability. Resolve material regressions before acceptance or record an explicit tradeoff with measured impact and a reason. Verify completed audit contents, fleet behavior and seeded headless output. |

Judge visual polish by readability, task completion and responsiveness. Record before-and-after captures and benchmark results for the packaged desktop.

## Phase 27 keyboard workflows and idle desktop verification

| Actor | Use case | Acceptance |
| --- | --- | --- |
| Operator | Find a saved audit with a keyboard | Open location search, type a shelf or Waco ID, and submit an exact match. Read the report, open Maintenance, and return to the same report with no pointer input. Commands that move a robot remain deliberate. |
| Reviewer | Verify the packaged keyboard path | Run the native search and navigation probe against seeded audit history and run it again from the extracted Windows package. |
| Performance reviewer | Measure idle Fleet independently | Measure an exclusively idle Fleet display after startup warmup. Record frame and tick percentiles, grid backing allocations and peak working set over repeated local Release runs. Record the separate-host and physical DPI gates as open when unavailable. |

## Phase 28 keyboard audit actions

| Actor | Use case | Acceptance |
| --- | --- | --- |
| Operator | Finish an audit review without a pointer | From a saved audit, focus Mark reviewed with the keyboard and explicitly activate it. Preserve the source report evidence and apply the usual eligibility checks. |
| Operator | Re-audit the selected location without a pointer | Focus Re-audit location with the keyboard and explicitly activate it. Use the selected available robot and retain the source audit link when the new report is saved. |
| Release reviewer | Verify deliberate activation | Exercise the native keyboard path with a saved Waco report and repeat it from the extracted Windows package. Verify the shortcuts alone submit no work, re-audit focus leaves active text entry alone, and the completed re-audit points to the reviewed report. |
