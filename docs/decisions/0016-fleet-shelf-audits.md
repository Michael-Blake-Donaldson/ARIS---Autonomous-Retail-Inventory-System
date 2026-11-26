# 0016 Run and archive the two-robot shelf showcase

**Status** Accepted for Phase 17

## Context

The desktop single-robot workbench owns one robot, sensor rig, mapper and active routed job. A shelf audit needs distinct robot and shelf identities, observation coverage, product-level review and durable history. Fleet rendering needs the assignments and observations for both robots as the jobs advance.

## Decision

Give each of the two showcase robots an independent live engine, seed, spawn point and single-job worker. Both engines run the same authored stockroom layout, each with its own simulation clock and world instance. The fleet map draws an observed estimate for each robot and the selected robot's observed route. Shelf placement and assignment are operator configuration. A robot's live data is published as an observation snapshot while its worker owns the engine.

Use the two physical shelves in the bundled stockroom as the complete set of fleet locations. A10J05 maps to the SOFTLINES site and A20J05 to the TECH site. Each site contains one modeled physical stock shelf. The shelf survey visits that site's two configured viewpoints and requires at least eight valid RFID observation frames per viewpoint. Existing aisle and search workflows keep their original survey ordering.

Snapshot names, DPCIs, expected quantities, uniquely observed and attributed quantities, and associated EPCs in the completed shelf report. Store per-location history in the mission journal using the transactional schema 3 migration. Atomically retain the latest 20 reports per location. Save writes use bound parameters and carry the robot, run, session and UTC completion time. Each successful retry with the same identity and content is idempotent. A conflicting retry is rejected. Review validates a versioned payload of up to 128 KiB before building the product list.

The three showcase product photos are bounded resources embedded in the Windows executable. Each is chosen by DPCI. Graphics context recreation reloads textures from the embedded resources.

## Consequences

Both robots can demonstrate concurrent independent audits. Their simulation instances do not model inter-robot collisions or radio interference. General warehouses can continue to use the original single-robot controls. The simulated RFID result identifies a configured site and cannot resolve multiple physical shelves within one site. Persistent use requires an operator-selected data directory. Opening a previous mission journal upgrades it to schema 3 while keeping existing missions and saved maps.
