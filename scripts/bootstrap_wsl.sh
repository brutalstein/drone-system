#!/usr/bin/env bash
set -euo pipefail

grep -qi "ubuntu" /etc/os-release || { echo "Ubuntu required" >&2; exit 2; }

sudo apt-get update
sudo apt-get install -y curl gnupg lsb-release software-properties-common git build-essential cmake ninja-build python3-pip python3-colcon-common-extensions python3-rosdep python3-vcstool qtbase5-dev libcurl4-openssl-dev nlohmann-json3-dev clang clang-tidy

if ! command -v ros2 >/dev/null 2>&1; then
  sudo curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key -o /usr/share/keyrings/ros-archive-keyring.gpg
  CODENAME=$( . /etc/os-release && echo "$UBUNTU_CODENAME" )
  echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu $CODENAME main" | sudo tee /etc/apt/sources.list.d/ros2.list >/dev/null
  sudo apt-get update
  sudo apt-get install -y ros-jazzy-desktop ros-jazzy-ros-gz
fi

sudo rosdep init 2>/dev/null || true
rosdep update
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT/ros2_ws"
rosdep install --from-paths src --ignore-src -r -y --rosdistro jazzy

grep -q "/opt/ros/jazzy/setup.bash" "$HOME/.bashrc" || echo "source /opt/ros/jazzy/setup.bash" >> "$HOME/.bashrc"
echo "Bootstrap complete."
