# Occupancy mapping and odometry

The mapper builds a planar occupancy grid from wheel and LiDAR observations. It accepts a configured rectangular extent and uses the pose estimate carried by the matching wheel observation. The desktop map view and headless output identify the method as **occupancy mapping + odometry**. The estimate remains wheel driven. The model has no automatic loop closure.

## Grid and localization

The southwest corner is the origin, with east as positive X and north as positive Y. World coordinates use meters, and the cell resolution is stored in integer millimeters. The map extent is half-open. Locate and center conversions handle partial edge cells. Resolution defaults to 250 mm. The supported range is 50–2,000 mm, the maximum extent is 100 m per axis and the maximum cell count is 1,000,000.

Each valid LiDAR hit contributes free evidence along its ray and occupied evidence at its endpoint. Nominal boundary hits mark the adjacent edge cell. Rays ending beyond the extent are clipped and contribute free evidence only. Missing returns add no evidence. The quantized log-odds model adds 6 for a hit and subtracts 2 for free space. Evidence saturates between −20 and +20. Values at or below −2 are free, values at or above +2 are occupied, and the remaining values are unknown.

Mapping suspends when the pose is outside the extent, odometry is unpowered, or the cumulative distance or heading error budget reaches its limit. The defaults are 4 m and 24 rad. These conservative budgets grow with observations and can pause mapping even when the actual pose error is small. The estimated pose stays available with its reliability flag.

A caller with an independently established pose can use `Mapper::reanchor`. It registers future odometry relative to that pose and clears the previous evidence. An accepted anchor must be inside the map and under both error thresholds. The desktop Home control resets the complete simulation run and mapper. F5 shows the estimated grid, with muted teal free cells, amber occupied cells, navy unknown space and a mint pose marker. A red pose marker indicates suspended mapping.

## Map replay and snapshots

`--map-steps N` runs a bounded survey and reports cell counts, the frame, and current pose reliability. `--export-map N` writes the estimated grid to binary stdout. Both accept 1–2,000 steps and an optional `--cell-mm N`. Use `--warehouse NAME --map-stdin` to load a binary snapshot with the matching fixture and cell size. The app uses shell redirection for files. C++ callers can use `mapping::save` and `mapping::load` with their own streams and trusted expected specification, including custom scenarios.

A snapshot stores eight magic bytes, a little-endian 32-bit version and frame identifier, a 64-bit scenario ID, two 32-bit dimensions in millimeters and a 32-bit cell resolution. The header is followed by one biased evidence byte per cell, in row-major order starting at the southwest, and a 32-bit FNV-1a checksum over the header and evidence. Readers verify the complete format and expected metadata before returning a grid. The checksum detects accidental corruption. Imported evidence is a static map from a run and contains no active localization state.

## Offline evaluation

The `argus-map-evaluation` test executable is built with `BUILD_TESTING=ON`. It compares the estimated occupied set with a raster of static obstacles and the warehouse boundary for occupancy IoU. It reports coverage as the fraction of cells with a classification and measures peak translation and heading error against the simulation. Moving actors can contribute to observations. The evaluator's reference occupancy is static. The production mapper does not link the simulation targets.
