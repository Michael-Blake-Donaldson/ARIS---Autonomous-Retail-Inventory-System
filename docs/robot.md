# Simulated robot controls and motion

The robot starts at the warehouse spawn, facing east. Positive heading turns counterclockwise toward north. The simulator advances in 50 ms steps. Robot poses use meters and radians, and warehouse collision queries use millimeters.

## Desktop controls

| Input | Behavior |
| --- | --- |
| Space and Up or Down | Hold Space to enable forward or reverse drive. |
| Space and Left or Right | Turn counterclockwise or clockwise. Combine with drive for an arc. |
| Release Space or leave the window | Brake toward zero. A new Space press re-enables control. |
| X | Stop immediately on the next simulation step and latch the stop. |
| R | Clear the stop with Space released. |
| Home | Reset the simulation pose, battery, and warehouse tick with Space released and the stop latch cleared. |
| F3 | Toggle the labeled developer truth overlay. |
| F4 | Toggle LiDAR returns projected from the estimate at capture time. |
| Escape | Close the preview. |

The mint disk shows the estimate from the two simulated wheel encoders. F3 adds the cyan true-pose outline. The title displays battery level and active faults. A large desktop scheduling delay caps physics catch-up at five steps and clears manual input.

## Motion and collision

The default command limits are 1.2 m/s and 2 rad/s. Axis acceleration limits are 0.8 m/s² and 3 rad/s². Braking limits are 1.8 m/s² and 5 rad/s². Direction reversal brakes to zero before applying opposite acceleration. The simulation clamps finite commands and rejects non-finite commands with a hard stop for that step. Emergency stops, collision rejection and unavailable power also take effect immediately at the step boundary.

The constant twist for each step integrates a differential-drive arc. A conservative chord margin covers curvature. The disk sweep checks static fixtures through the spatial index and tests moving actors in their relative frame. Boundary and obstacle contact are blocked. If stopping in place would also collide with an actor over the proposed world step, actor time holds at its prior tick while the robot settles. The preview indicates this condition. A safe move can release the hold. Battery and control continue to update.

## Energy and odometry

Energy is stored in integer microjoules. Each step accounts for base power, optional sensor draw and drive power scaled by the greater of normalized linear and angular speeds. Defaults are 20 Wh capacity, 12 Wh initial energy, a 4 Wh low threshold, 3 W idle, 2 W sensor draw, 24 W maximum drive draw and a 35 W charger. A motion that cannot be powered is rejected and the available pack energy is exhausted as a brownout. A stationary footprint fully inside the charging pad receives net charger power, including from empty, up to capacity.

Wheel rotations sample quantized left and right travel using a 0.36 m track and 1 mm encoder resolution. The estimator integrates wheel samples from the known spawn pose. The desktop and headless sensor replay use scenario-seeded encoder bias, jitter and slip. The headless drive baseline uses quantization alone. See the [sensor models](sensors.md).

Use `argus::robot::MotionController` for bounded commands and operational telemetry. `argus::simulation::RobotSimulation` owns the world collision adapter and supplies an explicit `debug_truth()` capability for simulation evaluators and the desktop overlay. The headless drive probe serializes the operational telemetry.
