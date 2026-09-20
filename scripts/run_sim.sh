#!/usr/bin/env bash
set -euo pipefail
COUNT="${1:-3}"
case "$COUNT" in (*[!0-9]*|"") echo "Drone count must be a positive integer." >&2; exit 2;; esac
[ "$COUNT" -gt 0 ] || exit 2
ROOT=$(cd "$(dirname "$0")/.." && pwd)
source /opt/ros/jazzy/setup.bash
source "$ROOT/ros2_ws/install/setup.bash"
ros2 launch drone_system_sim fleet.launch.py drone_count:="$COUNT"
