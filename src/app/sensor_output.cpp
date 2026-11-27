#include "sensor_output.hpp"

#include <array>
#include <iomanip>
#include <ostream>
#include <string_view>

namespace argus::app {
namespace {

constexpr std::array<std::string_view, 3> names{"wheel-encoder", "lidar", "rfid"};

void write_header(std::ostream &out, const sensors::Header &header) {
    const auto &pose = header.estimated_pose;
    out << "{\"sensorId\":\"" << names.at(sensors::index(header.sensor))
        << "\",\"provenance\":\"simulated\",\"sampleTick\":" << header.sample_tick
        << ",\"timestampMs\":" << header.timestamp_ms << ",\"worldTick\":" << header.world_tick
        << ",\"estimatedPose\":{\"xM\":" << pose.x.value() << ",\"yM\":" << pose.y.value()
        << ",\"headingRad\":" << pose.heading.value() << '}';
}

void write_payload(std::ostream &out, const sensors::WheelSample &sample) {
    out << ",\"leftDeltaM\":" << sample.reading.left_delta.value()
        << ",\"rightDeltaM\":" << sample.reading.right_delta.value()
        << ",\"distanceErrorM\":" << sample.reading.distance_error_m
        << ",\"headingErrorRad\":" << sample.reading.heading_error_rad
        << ",\"powered\":" << sample.powered;
}

void write_payload(std::ostream &out, const sensors::LidarScan &scan) {
    out << ",\"firstAngleRad\":" << scan.first_angle_rad
        << ",\"angleStepRad\":" << scan.angle_step_rad << ",\"rangeErrorM\":" << scan.range_error_m
        << ",\"returnCount\":" << scan.return_count << ",\"rangesM\":[";
    const char *separator = "";
    for (const auto &range : scan.ranges_m) {
        out << separator;
        if (range) {
            out << *range;
        } else {
            out << "null";
        }
        separator = ",";
    }
    out << ']';
}

void write_payload(std::ostream &out, const sensors::RfidScan &scan) {
    out << ",\"truncatedReads\":" << scan.truncated_reads << ",\"reads\":[";
    const char *separator = "";
    for (const auto &read : scan.reads) {
        // Warehouse validation restricts EPCs to uppercase hexadecimal digits.
        out << separator << "{\"epc\":\"" << read.epc << "\",\"signalDbm\":" << read.signal_dbm
            << '}';
        separator = ",";
    }
    out << ']';
}

} // namespace

void write_sensor_report(std::ostream &out, simulation::SensorRig &rig) {
    const auto backlog = rig.throughput().pending;
    out << std::fixed << std::setprecision(6) << std::boolalpha
        << ",\"sensors\":{\"queuedBeforeDrain\":" << backlog << ",\"observations\":[";
    const char *separator = "";
    while (const auto sample = rig.pop()) {
        out << separator;
        std::visit(
            [&out](const auto &value) {
                write_header(out, value.header);
                write_payload(out, value);
                out << '}';
            },
            *sample);
        separator = ",";
    }
    out << "],\"throughput\":{";
    const auto after = rig.throughput();
    separator = "";
    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto &stream = after.streams.at(index);
        out << separator << '\"' << names.at(index) << "\":{\"produced\":" << stream.produced
            << ",\"consumed\":" << stream.consumed << ",\"dropped\":" << stream.dropped << '}';
        separator = ",";
    }
    out << "}}";
}

} // namespace argus::app
