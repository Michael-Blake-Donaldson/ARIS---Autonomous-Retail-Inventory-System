# 0010 Separate the observed renderer from background operations

**Status** Accepted for Phase 10

## Context

The desktop needs live simulation feedback and an inventory and mission workbench. RFID and SQLite operations can take longer than a frame. Normal views must preserve the observation boundary and stay responsive while those jobs run.

## Decision

Use the pinned Dear ImGui SDL3 and OpenGL2 backends for a native window. Keep windowing, rendering, and the foreground robot on the main thread. Build bounded reusable mesh batches from declared fixture geometry. Use estimated pose, LiDAR, occupancy, and navigation snapshots for moving and overlay elements. Cart and worker truth are accessible only through an explicitly enabled Diagnostics frame. Procedural cartons are generic scene props and contain no EPC placement information.

Route panel commands through a bounded mailbox. The worker owns its SQLite connections and dedicated audit or mission simulator. The foreground renders completed result snapshots. Filter loaded catalog rows on demand, page repository queries, and clip rendered rows to the viewport.

Keep GL context and backend lifetimes scoped to the window. Reset motion commands before rebuilding the graphics context. Use a native framebuffer capture for the workflow recording so the capture contains only ARGUS.

## Consequences

The UI can remain interactive while a simulated background job runs. The foreground and workbench have separate robots. Declared geometry and estimated occupancy are distinct layers. Driver-specific graphics reset behavior must be verified on target hardware.
