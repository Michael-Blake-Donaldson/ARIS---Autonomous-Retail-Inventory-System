# Warehouse scenario format and interfaces

The warehouse model owns the simulated floor, collision geometry, actors and fictional EPC placements. The desktop uses an owned renderer snapshot. Headless replay prints scenario metadata and actor bounds for one tick.

## Coordinate and motion conventions

The southwest floor corner is the origin. Positive X points east, positive Y points north, and shelf height is measured upward from the floor. One meter is 1,000 integer millimeters. JSON points are `[x,y]`, and rectangles are `[x,y,width,height]` with the southwest point first. Internal rectangles use minimum and maximum corners.

The floor spans up to 100 m in each dimension. Walls, rack footprints and obstructions block robot motion. Aisles, doorway gaps, receiving, dock and charging are annotated regions. A warehouse contains at least one aisle and doorway and exactly one receiving, dock and charging zone. The charging zone sits inside its dock. Doorway gaps, the dock and the charging zone exclude solid interiors. Shelf heights increase from the bottom shelf. An item references a rack and one-based shelf, with an offset inside the rack footprint. An optional `headingDeg` sets RFID tag orientation from 0 through 359 degrees, counterclockwise from east. It defaults to zero.

The robot footprint is a disk with an authored radius between 1 mm and 5 m. Contact with any obstacle or the floor boundary is blocked. Adjacent static fixtures may touch along their edges. The validator rejects intersecting static fixture interiors, an obstructed robot spawn, and actor paths that touch static fixtures, other actor paths or the spawn disk.

Workers, carts and U-boats travel back and forth along an axis-aligned segment. The scenario seed and actor order determine the starting phase. Integer interpolation and the absolute tick make each position deterministic. A temporary obstruction has an `activeTicks` half-open interval `[from,until)`. Outside that interval it is absent from collision results, sensor queries, and renderer snapshots. Path validation uses each actor's full swept rectangle across its whole path.

## Import and export

[The version 1.0.0 scenario schema](../assets/warehouse/scenario.schema.json) describes the JSON structure. The runtime also validates semantic constraints, integer tokens, reference integrity and collision rules. It accepts at most 2 MiB of input, parser event depths 0 through 7, 200,000 parser events and strings up to 64 bytes. Each scenario can contain 128 static fixtures, 32 zones, 32 actors and 10,000 item placements. Each rack supports up to 16 shelves. Object IDs use short lowercase slugs. EPCs are unique 24-digit uppercase hexadecimal strings. The sensor read and audit evidence caps apply independently of scenario size.

Use `--scenario-stdin` to validate and load one document. The process reads at most 2 MiB plus one detection byte. An interactive pipe must close at end of document. The application accepts scenario bytes and catalogued fixture names. File selection and redirection are performed by the invoking shell or other caller. On Windows the process switches scenario input and fixture export to binary mode to preserve bytes.

Use `--export-warehouse NAME` to re-export a bundled fixture byte for byte. The Phase 1 envelope example remains a historical design fixture and lacks the geometry fields for a warehouse import.

## Interfaces

| Interface | Consumer | Data |
| --- | --- | --- |
| `Warehouse::from_json` and `Warehouse::from_stream` | Simulation entry points | Immutable validated warehouse or a typed scenario error |
| `Warehouse::collides` | Simulation physics | Disk contact at a tick, using a static spatial grid and bounded actor checks |
| `Warehouse::snapshot` | Renderer and explicit headless preview | Owned geometry, shelf heights, spawn and actor bounds |
| `SensorQuery` in `argus/simulation` | Simulation-side sensor generation | Obstacle rectangles and fictional placements within a query region |

The Windows preview receives the warehouse snapshot by const reference, alongside separate robot odometry. Blue shapes are racks, gray shapes are walls, and tan shapes are static obstructions. Yellow, orange and salmon mark workers, carts and staging equipment. The dock and charger use teal. Ground-truth item placements are separate from catalog records and stay in the simulation state and sensor capability. Future navigation and inventory modules should depend on measurement contracts.
