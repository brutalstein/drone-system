#include "drone_system_core/state_machine.hpp"

#include <cmath>

namespace drone_system_core {

StateMachine::StateMachine(Limits limits) : limits_(limits) {}

bool StateMachine::validate(const Command& c, std::string* rejection) const {
  const auto finite = [](double v) { return std::isfinite(v); };
  if (!finite(c.vx) || !finite(c.vy) || !finite(c.vz) ||
      !finite(c.yaw_rate) || !finite(c.takeoff_altitude_m) ||
      !finite(c.target_x) || !finite(c.target_y) || !finite(c.target_z) ||
      !finite(c.max_speed_mps)) {
    if (rejection) *rejection = "non-finite numeric field";
    return false;
  }
  if (std::hypot(c.vx, c.vy) > limits_.max_horizontal_speed_mps ||
      std::abs(c.vz) > limits_.max_vertical_speed_mps ||
      std::abs(c.yaw_rate) > limits_.max_yaw_rate_rps) {
    if (rejection) *rejection = "velocity/yaw limit exceeded";
    return false;
  }
  if (c.mode == Mode::Takeoff &&
      (c.takeoff_altitude_m <= 0.0 ||
       c.takeoff_altitude_m > limits_.max_takeoff_altitude_m)) {
    if (rejection) *rejection = "invalid takeoff altitude";
    return false;
  }
  if (c.mode == Mode::GotoPosition &&
      (c.target_z < 0.0 || c.target_z > limits_.max_takeoff_altitude_m ||
       c.max_speed_mps <= 0.0 ||
       c.max_speed_mps > limits_.max_horizontal_speed_mps)) {
    if (rejection) *rejection = "invalid position target or speed limit";
    return false;
  }
  if (c.mode == Mode::Velocity &&
      (c.lease < std::chrono::milliseconds(100) ||
       c.lease > std::chrono::milliseconds(2000))) {
    if (rejection) *rejection = "invalid velocity command lease";
    return false;
  }
  if (static_cast<std::uint8_t>(c.mode) >
      static_cast<std::uint8_t>(Mode::GotoPosition)) {
    if (rejection) *rejection = "unknown mode";
    return false;
  }
  return true;
}

bool StateMachine::accept(const Command& c, Clock::time_point now,
                          Clock::time_point manager_last_seen,
                          std::string* rejection) {
  if (!validate(c, rejection)) return false;
  if (have_sequence_ && c.sequence <= last_sequence_) {
    if (rejection) *rejection = "duplicate or reordered sequence";
    return false;
  }
  if (setpoint_.mode == Mode::EmergencyStop &&
      c.mode != Mode::EmergencyStop && c.mode != Mode::Hold) {
    if (rejection) *rejection = "emergency stop latched; send HOLD to reset";
    return false;
  }
  if (c.mode != Mode::EmergencyStop &&
      now - manager_last_seen >= limits_.hold_after) {
    if (rejection) *rejection = "control link unhealthy";
    return false;
  }

  have_sequence_ = true;
  last_sequence_ = c.sequence;
  setpoint_ = {
      c.mode,
      c.vx, c.vy, c.vz,
      c.yaw_rate,
      c.takeoff_altitude_m,
      c.target_x, c.target_y, c.target_z,
      c.max_speed_mps};
  velocity_lease_expires_ =
      c.mode == Mode::Velocity ? now + c.lease : Clock::time_point::min();
  failsafe_active_ = false;
  failsafe_reason_.clear();

  if (c.mode == Mode::EmergencyStop) {
    force(Mode::EmergencyStop, "operator emergency stop");
  }
  return true;
}

void StateMachine::force(Mode mode, const std::string& reason) {
  setpoint_.mode = mode;
  setpoint_.vx = 0.0;
  setpoint_.vy = 0.0;
  setpoint_.vz = 0.0;
  setpoint_.yaw_rate = 0.0;
  failsafe_active_ = true;
  failsafe_reason_ = reason;
}

void StateMachine::external_hold(const std::string& reason) {
  if (setpoint_.mode == Mode::EmergencyStop) return;
  force(Mode::Hold, reason);
}

void StateMachine::tick(Clock::time_point now,
                        Clock::time_point manager_last_seen,
                        double battery_pct) {
  if (setpoint_.mode == Mode::EmergencyStop) return;

  if (battery_pct <= limits_.min_battery_land_pct) {
    force(Mode::Land, "low battery");
    return;
  }

  const auto age = now - manager_last_seen;
  if (age >= limits_.land_after) {
    force(Mode::Land, "manager heartbeat lost");
    return;
  }
  if (age >= limits_.hold_after) {
    force(Mode::Hold, "manager heartbeat stale");
    return;
  }
  if (setpoint_.mode == Mode::Velocity &&
      now >= velocity_lease_expires_) {
    force(Mode::Hold, "velocity command lease expired");
  }
}

}  // namespace drone_system_core
