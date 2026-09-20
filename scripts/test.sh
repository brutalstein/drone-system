#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
source /opt/ros/jazzy/setup.bash
cd "$ROOT/ros2_ws"
colcon test --event-handlers console_direct+
colcon test-result --verbose
