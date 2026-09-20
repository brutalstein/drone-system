#!/usr/bin/env bash
set -euo pipefail

MODE="install"
SKIP_TESTS=0
for arg in "$@"; do
  case "$arg" in
    --doctor) MODE="doctor" ;;
    --skip-tests) SKIP_TESTS=1 ;;
    *) echo "Unknown option: $arg" >&2; exit 2 ;;
  esac
done

ROOT=$(cd "$(dirname "$0")/.." && pwd)
RUNTIME="$ROOT/runtime"
mkdir -p "$RUNTIME"

cyan='\033[38;5;45m'
green='\033[38;5;84m'
yellow='\033[38;5;214m'
red='\033[38;5;203m'
reset='\033[0m'
step(){ printf "$cyan◆$reset %s\n" "$1"; }
ok(){ printf "$green✓$reset %s\n" "$1"; }
warn(){ printf "$yellow!$reset %s\n" "$1"; }
fail(){ printf "$red✗$reset %s\n" "$1"; }

if ! grep -qi ubuntu /etc/os-release; then
  fail "Ubuntu is required for the supported ROS 2 / Gazebo stack."
  exit 3
fi

step "Inspecting Linux / WSL environment"
CPU_COUNT=$(nproc)
MEM_KB=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
MEM_GB=$((MEM_KB / 1024 / 1024))
[ "$MEM_GB" -lt 1 ] && MEM_GB=1
JOBS=$CPU_COUNT
MEM_JOBS=$((MEM_GB / 2))
[ "$MEM_JOBS" -lt 1 ] && MEM_JOBS=1
[ "$JOBS" -gt "$MEM_JOBS" ] && JOBS=$MEM_JOBS
[ "$JOBS" -gt 8 ] && JOBS=8
ok "CPU threads: $CPU_COUNT • RAM: ~$MEM_GB GiB • build workers: $JOBS"

BASE_PACKAGES="curl gnupg lsb-release software-properties-common git build-essential cmake ninja-build python3-pip python3-colcon-common-extensions python3-rosdep python3-vcstool qtbase5-dev libqt5svg5-dev libcurl4-openssl-dev nlohmann-json3-dev clang clang-tidy tmux jq pciutils"
ROS_PACKAGES="ros-jazzy-ros-base ros-jazzy-ros-gz"

have_pkg(){
  dpkg-query -W "$1" >/dev/null 2>&1
}

missing=""
for pkg in $BASE_PACKAGES $ROS_PACKAGES; do
  if ! have_pkg "$pkg"; then missing="$missing $pkg"; fi
done
missing=$(echo "$missing" | xargs || true)

if [ -n "$missing" ]; then
  warn "Missing packages:$missing"
  if [ "$MODE" = "doctor" ]; then
    warn "Doctor mode does not modify the system."
  else
    step "Installing only missing dependencies"
    sudo add-apt-repository universe -y >/dev/null
    if [ ! -f /usr/share/keyrings/ros-archive-keyring.gpg ]; then
      sudo curl -fsSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key -o /usr/share/keyrings/ros-archive-keyring.gpg
    fi
    if [ ! -f /etc/apt/sources.list.d/ros2.list ]; then
      CODENAME=$( . /etc/os-release && echo "$UBUNTU_CODENAME" )
      echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu $CODENAME main" | sudo tee /etc/apt/sources.list.d/ros2.list >/dev/null
    fi
    sudo apt-get update
    missing_after=""
    for pkg in $BASE_PACKAGES $ROS_PACKAGES; do
      if ! have_pkg "$pkg"; then missing_after="$missing_after $pkg"; fi
    done
    missing_after=$(echo "$missing_after" | xargs || true)
    if [ -n "$missing_after" ]; then
      sudo apt-get install -y --no-install-recommends $missing_after
    fi
    ok "Dependency installation complete"
  fi
else
  ok "All required apt packages are already installed; nothing downloaded."
fi

if command -v ros2 >/dev/null 2>&1; then
  ok "ROS 2 available"
else
  if [ "$MODE" = "doctor" ]; then
    fail "ROS 2 is unavailable."
  else
    fail "ROS 2 is still unavailable after dependency installation."
    exit 4
  fi
fi

if [ "$MODE" != "doctor" ]; then
  if [ ! -f /etc/ros/rosdep/sources.list.d/20-default.list ]; then
    step "Initializing rosdep"
    sudo rosdep init
  fi
  if [ ! -d "$HOME/.ros/rosdep/sources.cache" ]; then rosdep update; fi

  step "Resolving ROS package dependencies"
  cd "$ROOT/ros2_ws"
  rosdep install --from-paths src --ignore-src -r -y --rosdistro jazzy

  source /opt/ros/jazzy/setup.bash
  SOURCE_HASH=$(find src -type f \( -name '*.cpp' -o -name '*.hpp' -o -name 'CMakeLists.txt' -o -name 'package.xml' -o -name '*.msg' -o -name '*.py' -o -name '*.sdf' -o -name '*.svg' -o -name '*.qss' -o -name '*.qrc' \) -print0 | sort -z | xargs -0 sha256sum | sha256sum | awk '{print $1}')
  PREVIOUS_HASH=""
  if [ -f "$RUNTIME/source.hash" ]; then PREVIOUS_HASH=$(cat "$RUNTIME/source.hash"); fi

  if [ "$SOURCE_HASH" != "$PREVIOUS_HASH" ] || [ ! -f install/setup.bash ]; then
    step "Source changed; building with $JOBS bounded workers"
    export CMAKE_BUILD_PARALLEL_LEVEL="$JOBS"
    colcon build --symlink-install --parallel-workers "$JOBS" --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    if [ "$SKIP_TESTS" -eq 0 ]; then
      step "Running test suite"
      source install/setup.bash
      colcon test --parallel-workers "$JOBS" --event-handlers console_direct+
      colcon test-result --verbose
      ok "Tests passed"
    else
      warn "Tests skipped by request"
    fi
    printf '%s' "$SOURCE_HASH" > "$RUNTIME/source.hash"
  else
    ok "Source hash unchanged; existing verified build reused."
  fi
fi

step "Writing local system inventory"
REPORT="$RUNTIME/system-report-wsl.json"
python3 - "$REPORT" "$missing" "$JOBS" <<'PY'
import json, os, platform, subprocess, sys

def run(cmd):
    try:
        return subprocess.check_output(cmd, stderr=subprocess.STDOUT, text=True).strip()
    except Exception:
        return ""

rows=[]
for line in run(["dpkg-query","-W"]).splitlines():
    parts=line.split("\t",1)
    if len(parts)==2:
        rows.append({"name":parts[0],"version":parts[1]})

report={
    "generated_at_utc":run(["date","-u","+%Y-%m-%dT%H:%M:%SZ"]),
    "platform":platform.platform(),
    "kernel":platform.release(),
    "wsl_distro":os.getenv("WSL_DISTRO_NAME",""),
    "cpu_threads":os.cpu_count(),
    "memory":run(["bash","-lc","awk '/MemTotal/ {print $2 " kB"}' /proc/meminfo"]),
    "gpu":run(["bash","-lc","lspci 2>/dev/null | grep -Ei 'vga|3d|display' || true"]),
    "ros_distro":os.getenv("ROS_DISTRO",""),
    "ros2_path":run(["bash","-lc","command -v ros2 || true"]),
    "gazebo":run(["bash","-lc","gz sim --versions 2>/dev/null | head -n 1 || true"]),
    "cmake":run(["bash","-lc","cmake --version | head -n 1"]),
    "compiler":run(["bash","-lc","c++ --version | head -n 1"]),
    "build_workers":int(sys.argv[3]),
    "xai_key_configured":bool(os.getenv("XAI_API_KEY")),
    "missing_before_run":[x for x in sys.argv[2].split() if x],
    "installed_packages":rows,
}
with open(sys.argv[1],"w",encoding="utf-8") as f:
    json.dump(report,f,indent=2)
PY
ok "Inventory saved locally: $REPORT"

if [ "$MODE" = "doctor" ] && [ -n "$missing" ]; then exit 10; fi
ok "Smart bootstrap complete"
