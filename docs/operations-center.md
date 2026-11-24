# Native operations center

The Windows `argus-desktop` executable opens the SDL3/OpenGL operations center. Headless commands continue to work on Windows and Linux.

```bat
out\build\windows-release\argus-desktop.exe --warehouse rfid-audit
```

The live simulation starts in manual mode. Hold Space and use the arrow keys to drive. Release Space to brake. X latches the emergency stop. R clears the stop when Space is released. Home restarts the live run with Space released and the latch clear. Escape closes the window. The Explore button starts observation-driven frontier navigation. The brake is applied on loss of focus, minimization, stall, or graphics recreation.

Select Orbit, Top-down, or Follow with the camera buttons or keys 1, 2, and 3. Right-drag over the warehouse to orbit and scroll to zoom. Keyboard navigation is enabled for the panels. F4 toggles measured LiDAR. F5 opens the estimated map without authored meshes. F3 opens Diagnostics, where enabling the developer checkbox reveals the explicitly labeled simulated truth overlay. Returning to any other panel hides it.

| View | Workflow |
| --- | --- |
| Overview | Live battery, estimated pose, motion, LiDAR returns and RFID counts |
| Live Map | Layer toggles for LiDAR, occupancy, planned route, odometry trail, antenna region and uncertain evidence |
| Find Product | Lookup by fictional DPCI, page through assigned EPCs, filter catalog rows, and investigate likely aisles |
| Inventory Audit | Run an aisle audit and inspect expected quantities, unique EPC observations and review findings |
| Missions | Run aisle scans, return to dock, and record or confirm a manual inspection |
| Lineage / History | Inspect the latest mission's persisted state transitions |
| Settings | Select or reset the camera, reset the stop latch and restart the live simulation |
| Diagnostics | Inspect pose uncertainty, opt in to simulated truth, and recreate the graphics context |

Search, audit, and mission panels submit work to an isolated background simulator using the `rfid-audit` fixture. They return results to the foreground through a bounded mailbox. Their robots are separate from the live exploration view. Uncertain evidence regions overlay the live scene only if it is the bundled `rfid-audit` layout. The catalog uses 256-row pages and filters the loaded rows, up to 12,000. Viewport clipping limits the widgets emitted per frame. DPCI lookups can still query a product outside the loaded pages.

By default the workbench uses disposable fictional demo databases. To keep catalog and mission data across launches, use an existing operator-controlled directory with `argus-desktop --operations-data DIRECTORY`. It uses `catalog.db` and `missions.db` inside that directory and seeds the fictional catalog if the catalog database is absent. This standalone option opens the default live warehouse. A pre-existing mission database restores its latest record. The workbench rejects overlapping jobs and preserves unresolved mission state for cancellation or inspection confirmation.

## Local verification and recording

`argus-desktop --ui-smoke` drives the native workflows, loads a 12,000-row benchmark, resizes the window, and recreates OpenGL. It reports how many catalog rows were rendered and the 95th-percentile active frame time. Enable `ARGUS_ENABLE_NATIVE_UI_TESTS` at configure time to register it with CTest on an interactive Windows session. `--ui-tour` replays the same workflow at viewing speed.

`--ui-record` writes the native app framebuffer as bottom-up 1280 × 720 RGB24 at 20 frames per second. The workflow summary goes to stderr. Encode it from a binary-safe Command Prompt pipeline.

```bat
out\build\windows-release\argus-desktop.exe --ui-record | ffmpeg -f rawvideo -pix_fmt rgb24 -s 1280x720 -r 20 -i - -vf vflip -c:v libx264 -pix_fmt yuv420p out\phase-10-workflows.mp4
```

A graphics context creation or swap error is reported as a structured application failure after one attempted recovery. The native target uses a Windows OpenGL compatibility context. The headless targets have no SDL or Dear ImGui link dependencies.
