# Phase 4 sensor data boundary

The simulator performs sensor acquisition against a validated warehouse. Sensor consumers receive measurements and an estimated pose reference from the robot controller.

| Failure mode | Control | Verification |
| --- | --- | --- |
| Unbounded sensor workload | Validate beam count, sensor cadence, RFID dwell, maximum decoded reads and queue capacity. Limit headless replay to 2,000 steps. | Configuration, queue overflow and CLI bound tests |
| Missing, stale or duplicate acquisitions | Publish one acquisition per step and clear RFID dwell after a sample gap or power loss. Record timestamps and losses. | Cadence, power, dwell and monotonic acquisition tests |
| Ground-truth placement exposure | Publish EPC and simulated signal strength for a decoded read. Keep item position, rack references and true robot pose in simulation interfaces. | Observation type checks and CLI output verification |
| Numeric invalidity | Reject non-finite settings. Bound modeled error and range. Use nullable missing returns. | Input validation, range limits and grazing-ray tests |

Treat RFID observations as fallible detections. Repeated reads and missed reads are expected. Use fictional EPCs in fixtures and generated evidence.
