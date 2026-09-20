# Architecture

The project separates the control plane, simulation plant, operator UI, and AI advisor. No network call, UI callback, or AI inference is allowed inside a drone control loop.

## Control plane

- `fleet_manager`: operator-control heartbeat, command publisher, bounded fleet registry.
- `drone_brain`: one instance per simulated drone. Owns state machine, command validation, monotonic watchdog, and failsafe transitions.
- Shared topics carry `drone_id`, so fleet size is dynamic.

## Determinism and resources

Safety transitions use `std::chrono::steady_clock`. Commands include sequence and TTL. Duplicate sequence numbers and expired commands are rejected. ROS histories are bounded. Telemetry is last-value / best-effort; commands and heartbeats are bounded reliable streams. No blocking I/O is allowed on control-loop threads.

## AI boundary

Grok receives compact fleet snapshots and returns advisory text only. It has no actuator or command publisher. AI output is never part of the failsafe decision path.

## Real-aircraft boundary

This is a simulator-first research foundation. A real vehicle adapter requires separate HIL testing, independent autopilot failsafes, geofencing, and hardware-specific safety review.
