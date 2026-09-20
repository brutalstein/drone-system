#!/usr/bin/env bash
set -euo pipefail

COUNT=3
if [ "$#" -gt 0 ]; then COUNT="$1"; shift; fi
NO_GROK=0
NO_GAZEBO=0
for arg in "$@"; do
  case "$arg" in
    --no-grok) NO_GROK=1 ;;
    --no-gazebo) NO_GAZEBO=1 ;;
    *) echo "Unknown option: $arg" >&2; exit 2 ;;
  esac
done

case "$COUNT" in (*[!0-9]*|"") echo "Drone count must be a positive integer" >&2; exit 2;; esac
[ "$COUNT" -ge 1 ] && [ "$COUNT" -le 64 ] || { echo "Drone count must be 1..64" >&2; exit 2; }

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SESSION="drone-system"
RUNTIME="$ROOT/runtime"
LOGS="$RUNTIME/logs"
HEALTH="$RUNTIME/stack-health.json"
mkdir -p "$LOGS"

source /opt/ros/jazzy/setup.bash
source "$ROOT/ros2_ws/install/setup.bash"

if tmux has-session -t "$SESSION" 2>/dev/null; then
  tmux kill-session -t "$SESSION"
fi

if [ "$NO_GAZEBO" -eq 0 ]; then
  SIM_CMD="ros2 launch drone_system_sim fleet.launch.py drone_count:=$COUNT"
else
  SIM_CMD="ros2 run drone_system_core fleet_manager & ros2 run drone_system_sim fleet_simulator --ros-args -p drone_count:=$COUNT; wait"
fi

tmux new-session -d -s "$SESSION" -n simulation -c "$ROOT"
tmux set-option -t "$SESSION" remain-on-exit on
tmux send-keys -t "$SESSION:simulation" "source /opt/ros/jazzy/setup.bash; source '$ROOT/ros2_ws/install/setup.bash'; $SIM_CMD 2>&1 | tee '$LOGS/simulation.log'" C-m

DISPLAY_READY=0
if [ -n "${WAYLAND_DISPLAY:-}" ] || [ -n "${DISPLAY:-}" ]; then DISPLAY_READY=1; fi
if [ "$DISPLAY_READY" -eq 1 ]; then
  tmux new-window -t "$SESSION" -n console -c "$ROOT"
  tmux send-keys -t "$SESSION:console" "source /opt/ros/jazzy/setup.bash; source '$ROOT/ros2_ws/install/setup.bash'; sleep 2; ros2 run drone_system_ui fleet_console 2>&1 | tee '$LOGS/ui.log'" C-m
else
  echo "WSLg/display not detected; UI was not started." >&2
fi

KEY=$(printenv XAI_API_KEY || true)
GROK_STARTED=0
if [ "$NO_GROK" -eq 0 ] && [ -n "$KEY" ]; then
  GROK_STARTED=1
  tmux new-window -t "$SESSION" -n advisor -c "$ROOT"
  tmux send-keys -t "$SESSION:advisor" "source /opt/ros/jazzy/setup.bash; source '$ROOT/ros2_ws/install/setup.bash'; ros2 run drone_system_core grok_advisor 2>&1 | tee '$LOGS/grok.log'" C-m
fi

tmux new-window -t "$SESSION" -n health -c "$ROOT"
tmux send-keys -t "$SESSION:health" "source /opt/ros/jazzy/setup.bash; source '$ROOT/ros2_ws/install/setup.bash'; sleep 3; ros2 topic hz /fleet/telemetry" C-m

echo "Waiting for deterministic fleet health gate..."
set +e
python3 "$ROOT/scripts/health_probe.py" --drones "$COUNT" --timeout 18 --output "$HEALTH" >"$LOGS/health-probe.log" 2>&1
HEALTH_CODE=$?
set -e

if [ "$HEALTH_CODE" -ne 0 ]; then
  echo "Fleet health gate failed. See: $HEALTH" >&2
  cat "$LOGS/health-probe.log" >&2 || true
  tmux capture-pane -pt "$SESSION:simulation" -S -80 >"$LOGS/simulation-tail.log" 2>/dev/null || true
  tmux kill-session -t "$SESSION" 2>/dev/null || true
  exit "$HEALTH_CODE"
fi

UI_STARTED=0
if [ "$DISPLAY_READY" -eq 1 ] && pgrep -f "fleet_console" >/dev/null 2>&1; then UI_STARTED=1; fi
GAZEBO_STARTED=$((1-NO_GAZEBO))

cat > "$RUNTIME/stack.env" <<EOF
SESSION=$SESSION
DRONES=$COUNT
GAZEBO=$GAZEBO_STARTED
GROK=$GROK_STARTED
UI=$UI_STARTED
HEALTHY=1
STARTED_AT=$(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF

echo "Stack health gate passed."
echo "Stack started in tmux session '$SESSION' with $COUNT drone(s)."
echo "Health: $HEALTH"
echo "Logs: $LOGS"
