# 0014 Transfer the active simulation to routed work

**Status** Accepted for Phase 14

## Context

The desktop formerly launched a separate simulated robot for each search, audit, and mission task. The showcase requires one observable run from exploration through investigation, docking, and review. Storage and RFID work can take longer than a render frame.

## Decision

Keep one owned live engine. Transfer it through the existing single-job mailbox after foreground frame reads finish. The worker borrows its current robot, mapper, and sensor rig through the observed-run interface. It publishes a single replaceable observation snapshot every eight simulation ticks and returns ownership when the job finishes or is interrupted. Foreground rendering has no access to simulator truth while a job owns the engine.

Route shared desktop jobs through the bundled RFID audit stockroom. Reuse its authored survey viewpoints and charger and use a simulated 0.1 mm encoder with zero injected drift or slip. This keeps the existing uncertainty gate meaningful across several runs on the same simulation clock. The navigator's stop and reliability thresholds still apply.

Use the existing SQLite mission journal for a single run archive. Store map bytes, exact audit JSON, run and sample identifiers, and references to underlying work. The save is one transaction. Reopen the static snapshot using the map reader and validate its report and referenced records before displaying it.

## Consequences

The foreground can render progress while the worker owns the simulation. A routed job responds to hold, focus loss and emergency stop cooperatively between simulation steps. Manual control resumes after the worker returns ownership. The archive restores evidence for review. A new desktop process starts a new live run and has independent robot state.

Mission storage migration is forward-only. The archive accepts maps up to 128 KiB and reports up to 512 KiB. Report review also enforces a depth and parser-event budget. The saved archive records one capture point in the current run.
