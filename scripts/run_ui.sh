#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
source /opt/ros/jazzy/setup.bash
source "$ROOT/ros2_ws/install/setup.bash"
ros2 run drone_system_ui fleet_console
