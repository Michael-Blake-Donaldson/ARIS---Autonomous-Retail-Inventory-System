#pragma once

#include "argus/simulation/sensors.hpp"

#include <iosfwd>

namespace argus::app {

void write_sensor_report(std::ostream &out, simulation::SensorRig &rig);

} // namespace argus::app
