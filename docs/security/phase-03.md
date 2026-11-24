# Phase 3 motion and energy boundary

The desktop accepts keyboard debug commands. The headless runner accepts a bounded tick count for a scripted test drive. All motion targets and energy effects are confined to the simulator.

| Failure mode | Control | Verification |
| --- | --- | --- |
| Non-finite or excessive commands | Validate finiteness and clamp commands. Validate configuration before starting. | Invalid-configuration, saturation and emergency stop tests |
| Lost focus, delayed frames or continued commands after a stop | Disarm on focus loss or minimization and cap frame catch-up. Require released neutral control to reset the stop. | Dead-man and stop tests, plus Windows interaction checks |
| Thin barriers or crossing dynamic actors | Check a swept disk with arc clearance and actor-relative motion. Hold a conflicting world advance if necessary. | Thin-wall, actor sweep, hold/recovery and randomized tests |
| Energy underflow or overflow | Use checked configuration bounds, integer rates and saturating discharge and charge. | Exact energy, brownout, charging and cap tests |
| True pose in operational data | Use the narrow controller interface for telemetry. Keep true pose behind the explicit simulation debug capability. | Controller type check and headless output verification |

The controls and braking assumptions apply to simulated operation. A later hardware adapter requires a separate hazard and interface review.
