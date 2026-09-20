# Drone System

A simulator-first, multi-drone brain and fleet-control platform built around **ROS 2 Jazzy**, **Gazebo Harmonic**, **modern C++20**, and an optional **xAI Grok** operator-assistance layer.

> Status: foundation release. The project is designed for deterministic simulation, telemetry, fleet orchestration, fault injection, and operator-in-the-loop research. It is **not a certified flight controller** and must not be treated as safety certification for real aircraft.

## Design goals

- Dynamic fleet size: 1..N drones without hard-coded IDs.
- One operator console for every discovered drone.
- Shared ROS 2/DDS fleet bus for telemetry, commands, peer presence, and manager heartbeat.
- Per-drone watchdog with deterministic HOLD -> LAND failsafe on control-link loss.
- Bounded queues / bounded registry growth and fixed-rate loops to avoid unbounded resource consumption.
- Sequence-numbered, expiring, idempotent commands.
- Simulation telemetry: pose, velocity, battery, link quality, peer count, CPU / memory estimate, mode, armed state, failsafe state, heartbeat age.
- Fault injection for packet/link loss testing.
- Gazebo visualization synchronized from the deterministic kinematic simulator.
- Grok isolated from the flight-critical control path. AI can explain fleet state and assist the operator, but it never directly drives actuators.
- Windows-first onboarding through WSL2 + Ubuntu 24.04 for a reproducible ROS/Gazebo environment.
- CI gates for build, tests, formatting, static analysis and sanitizer builds.

## Supported baseline

The pinned reference stack is:

- Windows 11 host
- WSL2
- Ubuntu 24.04 LTS
- ROS 2 Jazzy Jalisco
- Gazebo Harmonic
- C++20
- Qt 5 Widgets
- xAI Responses API (optional)

Jazzy + Harmonic is intentionally chosen instead of chasing the newest ROS/Gazebo pair: it is an LTS-compatible, officially recommended pairing and is substantially easier to reproduce on developer machines.

## Repository layout

```text
.
├─ config/                         # Fleet/failsafe defaults
├─ docs/                           # Architecture, safety, protocol notes
├─ scripts/                        # Windows + WSL bootstrap / run helpers
├─ ros2_ws/src/
│  ├─ drone_system_interfaces/     # ROS messages/services
│  ├─ drone_system_core/           # Fleet manager, drone brain, Grok advisor
│  ├─ drone_system_sim/            # Deterministic simulation + Gazebo sync
│  └─ drone_system_ui/             # Dynamic Qt fleet control console
└─ .github/workflows/              # CI/security checks
```

## Quick start on Windows

Open **PowerShell as Administrator** once:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\bootstrap_windows.ps1
```

Then open the installed Ubuntu 24.04 WSL terminal:

```bash
cd ~/drone-system
./scripts/bootstrap_wsl.sh
./scripts/build.sh
./scripts/run_sim.sh 3
```

The last argument is the initial drone count. It is not a compile-time limit:

```bash
./scripts/run_sim.sh 1
./scripts/run_sim.sh 3
./scripts/run_sim.sh 12
```

The fleet manager accepts every valid drone ID that appears on the shared fleet bus until the configured safety cap is reached.

## Grok

Grok is optional and deliberately out-of-band from deterministic flight logic.

```bash
export XAI_API_KEY="..."
export DRONE_GROK_MODEL="grok-4.6"
ros2 run drone_system_core grok_advisor
```

The key is read only from the process environment. Never commit API keys.

## ROS topics

| Topic | QoS intent | Purpose |
|---|---|---|
| `/fleet/telemetry` | best effort / bounded | high-rate telemetry from every drone |
| `/fleet/heartbeat` | reliable | peer presence / liveness |
| `/fleet/manager_heartbeat` | reliable | control-plane watchdog source |
| `/fleet/command` | reliable | sequence-numbered operator commands |
| `/fleet/state` | reliable | aggregated fleet snapshot for UI / AI |

All shared messages include `drone_id`; no topic-per-drone discovery scheme is required.

## Built-in command modes

The foundation intentionally implements a small safety-oriented command set:

- HOLD
- TAKEOFF to a bounded altitude
- LAND
- RETURN_HOME
- VELOCITY setpoint with configured horizontal/vertical/yaw limits
- EMERGENCY_STOP for simulator safety testing

Commands carry an ID, issue time and TTL. Stale or duplicate commands are rejected by the drone brain.

## Link-loss policy

Each drone uses a monotonic watchdog independent of ROS simulated time.

1. Manager heartbeat healthy -> normal operation.
2. Manager heartbeat missing for `hold_after_ms` -> HOLD.
3. Missing for `land_after_ms` -> LAND.
4. A fresh manager heartbeat does not automatically resume the previous mission. The operator must explicitly command the drone again.

This avoids an unsafe "connection returned, continue old command" transition.

## Resource-management policy

- Registry entries are capped by `max_drones`.
- Telemetry and command paths use bounded queues or last-value state rather than unbounded buffering.
- No blocking network request is permitted on control-loop threads.
- Timers run at explicit frequencies.
- AI calls run on their own worker and use hard timeouts / input limits.
- DDS QoS is selected by data criticality rather than making every stream reliable.
- Simulation uses fixed integration steps and clamps acceleration / velocity.
- Shutdown uses RAII and ROS executors are bounded to a configured thread count.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/SAFETY.md](docs/SAFETY.md).

## Validation

```bash
./scripts/build.sh
./scripts/test.sh
```

CI also runs build/test and static checks on every push and pull request.

## Scope

The repository is intended for civilian robotics research, education, fleet-management simulation and operator tooling. It does not include autonomous targeting, weapon payload integration, or code intended to select or engage people or objects.

## License

Apache-2.0. See [LICENSE](LICENSE).
