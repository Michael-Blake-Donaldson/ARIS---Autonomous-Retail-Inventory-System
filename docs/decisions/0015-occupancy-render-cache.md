# 0015 Cache occupancy vertices by observed map revision

**Status** Accepted for Phase 15

## Context

The native renderer visited the displayed occupancy grid and constructed its vertices on each visible frame. A map can be drawn multiple times between simulation updates. The desktop worker publishes copies of its observed map, so object addresses are unsuitable as revision identifiers.

## Decision

Advance a mapper-local revision when an accepted scan can contribute evidence and when the mapper clears the grid. Carry that revision and run identity into live and worker frames. Assign each successfully reopened archive a display revision in a separate domain.

Keep a single contiguous occupancy vertex buffer in the scene. Rebuild it when the grid specification, run identity, display source or revision changes. Reuse the existing observation-based geometry builder and its display stride. The buffer retains capacity for subsequent rebuilds and its maximum size follows the existing validated grid and display stride limits.

## Consequences

The scene can reuse geometry across render frames and across copies of the same observation. Switching between live and archived evidence, restarting a run or clearing a map triggers a rebuild. An accepted scan can trigger a rebuild even when its evidence saturates at the existing values. The scene continues to submit visible geometry through OpenGL on each drawn frame. The CPU buffer holds no graphics context resources and survives graphics context recreation. The map format and archive schema require no revision field.
