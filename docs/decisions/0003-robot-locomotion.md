# Use fixed step differential drive and conservative collision handling

**Status** Accepted

## Context

The simulated robot must stop at warehouse boundaries and moving obstacles while its command input, estimated pose and physical state remain independent. The headless and desktop applications share the same motion model.

## Decision

Clamp requested linear and angular velocities, then apply separate acceleration and braking limits at each 50 ms update. Integrate the twist as an arc. Check a swept disk against static geometry and translating actors with a conservative allowance for arc curvature. Reject unsafe motion first. If the stationary footprint also conflicts with the next actor update, hold world time for that step to preserve the last safe state.

Integrate quantized differential wheel travel for odometry. Publish commands, the estimate, sampled velocities and energy through the controller interface. Supply true pose through the simulation's debug capability only. Use integer microjoules for battery accounting and require full stationary footprint contact with the charging zone.

## Alternatives

Endpoint-only checks can miss thin geometry and between-frame actor movement. Increasing the physics rate reduces the distance per sample and still leaves that gap. Resolving impacts by moving actors or the robot off their planned path would add a contact mechanics model and could obscure failed commands.

## Consequences

A held actor can stall world time until the robot can move safely or a developer restarts the simulation. Collision rejection stops translation and rotation for the step. The fixed-twist integrator models acceleration between samples and omits traction and motor transients. Builds use their native floating-point libraries for trigonometry. No dependencies were added.
