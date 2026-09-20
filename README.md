# AERION Drone System

A Windows-first, simulator-first multi-drone fleet brain built with ROS 2 Jazzy, Gazebo Harmonic, modern C++20, Qt and an optional xAI Grok advisory layer.

## One-command Windows start

Clone the repository and run:

~~~powershell
.\START-DRONE-SYSTEM.ps1
~~~

You can also double-click START-DRONE-SYSTEM.cmd.

The launcher performs a local Windows + WSL inventory, detects software that is already installed, installs only missing prerequisites, preserves build caches, chooses a bounded build parallelism from CPU/RAM, tests changed source and launches the stack under tmux.

Useful commands:

~~~powershell
.\START-DRONE-SYSTEM.ps1 -Drones 8
.\START-DRONE-SYSTEM.ps1 -Doctor
.\START-DRONE-SYSTEM.ps1 -Stop
.\START-DRONE-SYSTEM.ps1 -NoGrok
.\START-DRONE-SYSTEM.ps1 -NoGazebo
~~~

The generated system reports stay local under runtime/ and are ignored by Git.

## Premium operations console

The AERION Qt console is designed for operators rather than developers. It includes:

- live auto-scaling fleet radar
- dynamic N-drone discovery
- active fleet, average battery, link health and failsafe KPI cards
- position, velocity, battery, link, peer, armed, failsafe and heartbeat telemetry
- one-drone or all-drone target selection
- TAKEOFF, LAND, RETURN HOME, HOLD and EMERGENCY STOP
- press-and-hold movement controls that return to HOLD on release
- W/A/S/D movement, R/F altitude, Q/E yaw and Space HOLD shortcuts
- configurable horizontal / vertical speed, yaw rate and takeoff altitude
- deterministic command palette such as "takeoff 5" and "vel 1 0 0 0.2"
- Grok fleet-advisor panel with no command-publisher access
- local operations event log

Every flight command still passes through the sequence, TTL and control-envelope validation in the core state machine.

## Link-loss behavior

1. Healthy manager heartbeat: normal command operation.
2. Heartbeat reaches the HOLD threshold: the drone enters HOLD.
3. Heartbeat reaches the LAND threshold: the drone enters LAND.
4. A restored connection does not silently resume an old movement command.
5. Low simulated battery forces LAND.

Duplicate or reordered commands, expired commands, non-finite values and out-of-envelope motion requests are rejected.

## Stack

- Windows 11 host
- WSL2 + Ubuntu 24.04
- ROS 2 Jazzy
- Gazebo Harmonic
- C++20
- Qt 5 Widgets + SVG
- xAI Grok 4.6, optional

## Repository layout

~~~text
.
├─ START-DRONE-SYSTEM.ps1
├─ START-DRONE-SYSTEM.cmd
├─ config/
├─ docs/
├─ scripts/
└─ ros2_ws/src/
   ├─ drone_system_interfaces/
   ├─ drone_system_core/
   ├─ drone_system_sim/
   └─ drone_system_ui/
~~~

## Grok

Set the xAI key in the Windows environment before launch:

~~~powershell
$env:XAI_API_KEY="..."
$env:DRONE_GROK_MODEL="grok-4.6"
.\START-DRONE-SYSTEM.ps1
~~~

The key value is not written to system reports or committed to Git.

## Manual WSL flow

~~~bash
bash scripts/smart_bootstrap.sh
bash scripts/launch_stack.sh 3
bash scripts/stop_stack.sh
~~~

## Scope

This repository is a civilian robotics simulation and fleet-operations foundation, not aviation certification. It does not include autonomous targeting, weapon integration or engagement logic.

## License

Apache-2.0.
