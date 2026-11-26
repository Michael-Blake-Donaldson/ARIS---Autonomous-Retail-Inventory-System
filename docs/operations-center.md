# Native operations center

The Windows `argus-desktop` executable opens the SDL3/OpenGL operations center. Headless commands continue to work on Windows and Linux.

```bat
out\build\windows-release\argus-desktop.exe --warehouse rfid-audit
```

The default RFID stockroom opens to **Fleet**. Expand **Single robot** and select **Overview** to drive the original manual robot. Hold Space and use the arrow keys to drive. Release Space to brake. X latches the manual robot's emergency stop and holds fleet audits. R clears the manual stop when Space is released. Home restarts the manual run with Space released and the latch clear. Escape closes the window. The Explore button starts observation-driven frontier navigation. The brake is applied on loss of focus, minimization, stall, or graphics recreation.

In 3D views, select Orbit, Top-down, or Follow with the camera buttons or keys 1, 2, and 3. Right-drag to orbit and scroll to zoom. Keyboard navigation is enabled for the panels. F4 toggles measured LiDAR in single-robot views. F5 opens the estimated map without authored meshes. F3 opens Diagnostics, where enabling the developer checkbox reveals the explicitly labeled simulated truth overlay. Returning to any other panel hides it.

| View | Workflow |
| --- | --- |
| Overview | Live battery, estimated pose, motion, LiDAR returns and RFID counts |
| Live Map | Scan age, localization bounds, navigation decisions and layer toggles for LiDAR, occupancy, route, trail and evidence |
| Find Product | Lookup by fictional DPCI, page through assigned EPCs, filter catalog rows, and investigate likely aisles |
| Inventory Audit | Run an aisle audit and inspect expected quantities, unique EPC observations and review findings |
| Missions | Run aisle scans, return to dock, and record or confirm a manual inspection |
| Lineage / History | Inspect the latest mission's persisted state transitions |
| Settings | Select or reset the camera, reset the stop latch and restart the live simulation |
| Diagnostics | Inspect pose uncertainty, opt in to simulated truth, and recreate the graphics context |
| Fleet | See both simulated robots, battery, assigned shelves and the selected robot's observed route |
| Robot Maintenance | Select a robot and inspect its full-brightness LiDAR, motion trail, localization bounds and current navigation decision |
| Location Audits | Review coverage by shelf and open completed audits with product photos, counts and RFID tags |

The desktop starts in the bundled `rfid-audit` stockroom. Explore, product investigation, aisle audit, and routed mission jobs use the same simulated robot and its existing map. During a routed job, the background worker owns the run and publishes observed map and route updates to the dashboard. The elapsed-time readout uses simulation time. Use **Hold routed work** to interrupt a job. Focus loss and the emergency-stop control also suspend routed motion. Resolve an unfinished mission before starting another routed job.

Other warehouses support live driving and mapping. The bundled RFID stockroom supplies the authored survey viewpoints and dock for routed work. Catalog lookup is available independently. The catalog uses 256-row pages and filters the loaded rows, up to 12,000. Viewport clipping limits rendered rows and DPCI lookups cover the full catalog.

## Run a two-robot shelf audit

1. In **Fleet**, select a robot and an aisle. The four aisle summaries count completed locations and flag reports that need review. Choose **Browse locations** to open the selected aisle. A whole shelf such as `A10J05` is one audit. Expand `A20J06` to choose one of its ten Wacos, such as `A20J06-W01`.
2. Select **Start location audit**. A free robot can audit any listed location. Choose Robot 2 to start a different location while the first job runs. The same location can have only one active job. To see a shared-aisle yield, assign Robot 1 to `A20J05` and Robot 2 to `A20J06-W01`. The second robot shows **Yielding** at its berth and starts when the first robot clears the shared route.
3. **Open audit** from Fleet or search the full ID in **Location Audits**. A whole shelf or Waco becomes **Complete** once both views meet the coverage threshold and the report is saved. Its **Needs review** status reflects discrepancies or unresolved evidence. A divided shelf shows how many of its Wacos are complete and contributes only those Wacos to the aisle totals.
4. Compare photos, DPCIs, expected and observed quantities. Expand **RFID tags**, **Unresolved RFID reads** or **Audit history** for the underlying details. Unresolved reads are excluded from the observed count.
5. Choose **Inspect robot** for **Robot Maintenance**. It keeps the expanded stockroom scene, original LiDAR rays and full motion trail. Expand **Sensor details** for counters and localization bounds. Fleet shows a hold and its reason beside the affected robot. Use **Hold fleet** to interrupt active jobs. Losing focus also holds them. If a robot stops away from its berth, select it and choose **Return to berth** to clear its aisle reservation, then restart the audit if needed.

To retain shelf audits across launches, use the same existing `--operations-data DIRECTORY` each time. Otherwise Location Audits uses temporary session history. The last 20 completed audits for each shelf are retained. [Model and storage decisions](decisions/0016-fleet-shelf-audits.md)

## Save and reopen a run

By default the workbench uses disposable fictional demo databases. To keep evidence across launches, start `argus-desktop --operations-data DIRECTORY` with an existing operator-controlled directory. The desktop opens `catalog.db` and `missions.db` and seeds the fictional catalog when its database is absent. A previously persisted mission remains available as earlier work.

1. Explore the RFID audit stockroom and run a product investigation or aisle audit from the corresponding panel.
2. After an aisle audit, open **Lineage / History** and select **Save map and audit**. This replaces the last saved run and records the run ID, sample tick, mission reference when present, search and audit session references, occupancy snapshot, and exact audit JSON.
3. Select **Open saved run** to view the archived map and read the audit report. Use **Copy audit JSON** for the saved export, or **Return to live** to leave review.

Saved runs are read-only evidence. Reopening an archive validates its map and referenced records. Start the same desktop mode against the same data directory to reopen it after a restart.

## Read the live map

The Live Map reports the age of the last LiDAR scan accepted by the mapper, measured in simulation time. With the 50 ms step, a scan is fresh through 150 ms. Expired rays disappear while accumulated occupancy stays visible. The robot marker is mint for a current localization estimate, amber for an uncertain estimate, and gray while waiting for odometry. The error bounds are model estimates for distance and heading.

Explore shows the navigator's current state and decision, such as holding for a fresh scan or yielding to an obstruction. The panel also shows yield and replan counts and the last recorded replan cause. These counters describe the current autonomous run.

Use **Simulate LiDAR dropout** in Live Map to withhold new scans. During autonomous exploration, the navigator holds after the last scan expires. Clear the checkbox to let fresh observations reach the mapper and navigator again. Wheel and RFID observations continue during the fault.

## Local verification and recording

`argus-desktop --ui-smoke` drives the native workflows, verifies a LiDAR dropout and recovery, loads a 12,000-row benchmark, resizes the window, and recreates OpenGL. It reports visible catalog rows, p95 and p99 active frame and scene times, and the counts of occupancy frames, geometry builds and generated vertices. Scene time measures the CPU-side duration of scene drawing and graphics calls before the swap. Enable `ARGUS_ENABLE_NATIVE_UI_TESTS` at configure time to register it with CTest on an interactive Windows session. `--ui-tour` replays the same workflow at viewing speed.

On an interactive test host, pass an existing disposable directory to `--ui-smoke DIRECTORY` and then run `--ui-reopen DIRECTORY` in a second process to verify a durable archive.

Run `--ui-fleet-smoke DIRECTORY` on a fresh existing disposable directory and then `--ui-fleet-reopen DIRECTORY` to verify both shelf reports after a restart. The reopen test clicks the zoom and shelf controls, opens the audit in two clicks from Fleet, switches to Maintenance with the same selection, and verifies a hold appears in Fleet at 1280 × 720. Run `--ui-fleet-traffic DIRECTORY` with a second fresh directory to verify yielding and completion of two A20 audits. The CTest entry `argus-desktop.fleet` runs all three commands when native tests are enabled.

Use `--ui-dashboard-record DIRECTORY` with the same disposable data to record the three dashboard views during the scripted review. It uses the framebuffer and stderr format below.

`--ui-record` writes the native app framebuffer as bottom-up 1280 × 720 RGB24 at 20 frames per second. The workflow summary goes to stderr. Encode it from a binary-safe Command Prompt pipeline.

```bat
out\build\windows-release\argus-desktop.exe --ui-record | ffmpeg -f rawvideo -pix_fmt rgb24 -s 1280x720 -r 20 -i - -vf vflip -c:v libx264 -pix_fmt yuv420p out\phase-10-workflows.mp4
```

A graphics context creation or swap error is reported as a structured application failure after one attempted recovery. The native target uses a Windows OpenGL compatibility context. The headless targets have no SDL or Dear ImGui link dependencies.
