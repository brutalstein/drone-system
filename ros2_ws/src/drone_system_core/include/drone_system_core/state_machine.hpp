#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace drone_system_core {

enum class Mode : std::uint8_t {
  Hold = 0,
  Takeoff = 1,
  Land = 2,
  ReturnHome = 3,
  Velocity = 4,
  EmergencyStop = 5,
};

struct Limits {
  double max_horizontal_speed_mps{8.0};
  double max_vertical_speed_mps{3.0};
  double max_yaw_rate_rps{1.2};
  double max_takeoff_altitude_m{30.0};
  double min_battery_land_pct{12.0};
  std::chrono::milliseconds hold_after{1200};
  std::chrono::milliseconds land_after{5000};
};

struct Command {
  std::uint64_t sequence{0};
  Mode mode{Mode::Hold};
  double vx{0.0};
  double vy{0.0};
  double vz{0.0};
  double yaw_rate{0.0};
  double takeoff_altitude_m{0.0};
  std::chrono::milliseconds lease{1000};
};

struct Setpoint {
  Mode mode{Mode::Hold};
  double vx{0.0};
  double vy{0.0};
  double vz{0.0};
  double yaw_rate{0.0};
  double takeoff_altitude_m{0.0};
};

class StateMachine {
 public:
  using Clock = std::chrono::steady_clock;
  explicit StateMachine(Limits limits = {});

  bool accept(const Command& command, Clock::time_point now,
              Clock::time_point manager_last_seen, std::string* rejection = nullptr);
  void tick(Clock::time_point now, Clock::time_point manager_last_seen,
            double battery_pct);
  void external_hold(const std::string& reason);

  [[nodiscard]] const Setpoint& setpoint() const noexcept { return setpoint_; }
  [[nodiscard]] bool failsafe_active() const noexcept { return failsafe_active_; }
  [[nodiscard]] const std::string& failsafe_reason() const noexcept { return failsafe_reason_; }
  [[nodiscard]] std::uint64_t last_sequence() const noexcept { return last_sequence_; }

 private:
  bool validate(const Command& command, std::string* rejection) const;
  void force(Mode mode, const std::string& reason);

  Limits limits_;
  Setpoint setpoint_;
  std::uint64_t last_sequence_{0};
  Clock::time_point velocity_lease_expires_{Clock::time_point::min()};
  bool have_sequence_{false};
  bool failsafe_active_{false};
  std::string failsafe_reason_;
};

}  // namespace drone_system_core
