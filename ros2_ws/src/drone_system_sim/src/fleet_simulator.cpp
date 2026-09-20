#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <geometry_msgs/msg/twist.hpp>
#include <rclcpp/rclcpp.hpp>

#include <drone_system_core/state_machine.hpp>
#include <drone_system_interfaces/msg/drone_telemetry.hpp>
#include <drone_system_interfaces/msg/fleet_command.hpp>
#include <drone_system_interfaces/msg/heartbeat.hpp>

using drone_system_core::Command;
using drone_system_core::Mode;
using drone_system_core::StateMachine;

namespace {

struct Drone {
  explicit Drone(std::string value, drone_system_core::Limits limits)
      : id(std::move(value)), brain(limits) {}

  std::string id;
  StateMachine brain;
  double x{0.0}, y{0.0}, z{0.0}, yaw{0.0};
  double vx{0.0}, vy{0.0}, vz{0.0}, yaw_rate{0.0};
  double battery{100.0};
  std::uint64_t telemetry_seq{0};
  std::uint64_t heartbeat_seq{0};
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr gazebo_cmd_pub;
};

double approach(double current, double target, double max_delta) {
  const double delta = std::clamp(target - current, -max_delta, max_delta);
  return current + delta;
}

}  // namespace

class FleetSimulator final : public rclcpp::Node {
 public:
  FleetSimulator() : Node("fleet_simulator") {
    const int requested = declare_parameter<int>("drone_count", 3);
    max_drones_ = declare_parameter<int>("max_drones", 64);
    const int count = std::clamp(requested, 1, max_drones_);
    step_ms_ = declare_parameter<int>("sim_step_ms", 20);
    peer_timeout_ms_ = declare_parameter<int>("peer_timeout_ms", 1500);
    const double peer_heartbeat_hz =
        declare_parameter<double>("peer_heartbeat_hz", 2.0);

    drone_system_core::Limits limits;
    limits.max_horizontal_speed_mps =
        declare_parameter<double>("max_horizontal_speed_mps", 8.0);
    limits.max_vertical_speed_mps =
        declare_parameter<double>("max_vertical_speed_mps", 3.0);
    limits.max_yaw_rate_rps =
        declare_parameter<double>("max_yaw_rate_rps", 1.2);
    limits.max_takeoff_altitude_m =
        declare_parameter<double>("max_takeoff_altitude_m", 30.0);
    limits.min_battery_land_pct =
        declare_parameter<double>("min_battery_land_pct", 12.0);
    limits.hold_after =
        std::chrono::milliseconds(declare_parameter<int>("hold_after_ms", 1200));
    limits.land_after =
        std::chrono::milliseconds(declare_parameter<int>("land_after_ms", 5000));
    limits_ = limits;

    auto telemetry_qos = rclcpp::QoS(rclcpp::KeepLast(8)).best_effort();
    auto reliable = rclcpp::QoS(rclcpp::KeepLast(32)).reliable();

    telemetry_pub_ =
        create_publisher<drone_system_interfaces::msg::DroneTelemetry>(
            "/fleet/telemetry", telemetry_qos);
    peer_heartbeat_pub_ =
        create_publisher<drone_system_interfaces::msg::Heartbeat>(
            "/fleet/heartbeat", reliable);

    command_sub_ =
        create_subscription<drone_system_interfaces::msg::FleetCommand>(
            "/fleet/command", reliable,
            [this](drone_system_interfaces::msg::FleetCommand::ConstSharedPtr msg) {
              on_command(*msg);
            });

    manager_heartbeat_sub_ =
        create_subscription<drone_system_interfaces::msg::Heartbeat>(
            "/fleet/manager_heartbeat", reliable,
            [this](drone_system_interfaces::msg::Heartbeat::ConstSharedPtr) {
              if (!drop_manager_link_) {
                manager_last_seen_ = StateMachine::Clock::now();
              }
            });

    peer_heartbeat_sub_ =
        create_subscription<drone_system_interfaces::msg::Heartbeat>(
            "/fleet/heartbeat", reliable,
            [this](drone_system_interfaces::msg::Heartbeat::ConstSharedPtr msg) {
              if (is_known_drone(msg->source_id)) {
                peer_last_seen_[msg->source_id] = StateMachine::Clock::now();
              }
            });

    drop_manager_link_ =
        declare_parameter<bool>("fault_drop_manager_link", false);
    drop_peer_link_ =
        declare_parameter<bool>("fault_drop_peer_link", false);
    manager_last_seen_ = StateMachine::Clock::now();
    peer_guard_started_ = StateMachine::Clock::now();

    drones_.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      Drone drone("drone_" + std::to_string(i + 1), limits_);
      drone.x = static_cast<double>(i % 4) * 2.5;
      drone.y = static_cast<double>(i / 4) * 2.5;
      drone.gazebo_cmd_pub =
          create_publisher<geometry_msgs::msg::Twist>(
              "/model/" + drone.id + "/cmd_vel",
              rclcpp::QoS(4).best_effort());
      drones_.push_back(std::move(drone));
    }

    const auto peer_period = std::chrono::duration<double>(
        1.0 / std::max(0.5, peer_heartbeat_hz));
    peer_timer_ = create_wall_timer(
        std::chrono::duration_cast<std::chrono::milliseconds>(peer_period),
        [this] { publish_peer_heartbeats(); });
    timer_ = create_wall_timer(
        std::chrono::milliseconds(step_ms_), [this] { step(); });

    RCLCPP_INFO(get_logger(),
                "Fleet simulator online with %zu drones (cap=%d, peer heartbeat %.1f Hz)",
                drones_.size(), max_drones_, peer_heartbeat_hz);
  }

 private:
  bool is_known_drone(const std::string& id) const {
    return std::any_of(drones_.begin(), drones_.end(),
                       [&id](const Drone& d) { return d.id == id; });
  }

  std::uint32_t fresh_peer_count(
      const std::string& self_id, StateMachine::Clock::time_point now) const {
    std::uint32_t count = 0;
    const auto timeout = std::chrono::milliseconds(peer_timeout_ms_);
    for (const auto& [id, last_seen] : peer_last_seen_) {
      if (id != self_id && now - last_seen < timeout) ++count;
    }
    return count;
  }

  void publish_peer_heartbeats() {
    if (drop_peer_link_) return;
    for (auto& d : drones_) {
      drone_system_interfaces::msg::Heartbeat heartbeat;
      heartbeat.source_id = d.id;
      heartbeat.sequence = ++d.heartbeat_seq;
      heartbeat.stamp = now();
      peer_heartbeat_pub_->publish(heartbeat);
    }
  }

  void on_command(const drone_system_interfaces::msg::FleetCommand& msg) {
    const auto ros_now = now();
    const rclcpp::Time issued(msg.issued_at);
    const auto age_ns = (ros_now - issued).nanoseconds();
    if (msg.ttl_ms == 0 || age_ns < 0 ||
        age_ns > static_cast<std::int64_t>(msg.ttl_ms) * 1000000LL) {
      RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 2000, "Rejected stale command");
      return;
    }

    Command c;
    c.sequence = msg.sequence;
    c.mode = static_cast<Mode>(msg.mode);
    c.vx = msg.linear.x;
    c.vy = msg.linear.y;
    c.vz = msg.linear.z;
    c.yaw_rate = msg.yaw_rate;
    c.takeoff_altitude_m = msg.takeoff_altitude_m;

    for (auto& d : drones_) {
      if (msg.drone_id != "*" && msg.drone_id != d.id) continue;
      std::string why;
      if (!d.brain.accept(
              c, StateMachine::Clock::now(), manager_last_seen_, &why)) {
        RCLCPP_WARN(
            get_logger(), "%s rejected command: %s",
            d.id.c_str(), why.c_str());
      }
    }
  }

  void step() {
    const auto steady_now = StateMachine::Clock::now();
    const double dt = static_cast<double>(step_ms_) / 1000.0;
    const auto hb_age =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            steady_now - manager_last_seen_);
    const auto expected_peers =
        drones_.empty() ? 0U : static_cast<std::uint32_t>(drones_.size() - 1);
    const bool peer_guard_armed =
        steady_now - peer_guard_started_ >=
        std::chrono::milliseconds(peer_timeout_ms_);

    for (auto& d : drones_) {
      const std::uint32_t peer_count =
          fresh_peer_count(d.id, steady_now);
      if (peer_guard_armed && expected_peers > 0U &&
          peer_count < expected_peers) {
        d.brain.external_hold("peer heartbeat degraded");
      }

      d.brain.tick(steady_now, manager_last_seen_, d.battery);
      const auto& sp = d.brain.setpoint();

      double tx = 0.0, ty = 0.0, tz = 0.0, tyaw = 0.0;
      switch (sp.mode) {
        case Mode::Velocity:
          tx = sp.vx;
          ty = sp.vy;
          tz = sp.vz;
          tyaw = sp.yaw_rate;
          break;
        case Mode::Takeoff:
          tz = d.z + 0.05 < sp.takeoff_altitude_m ? 1.2 : 0.0;
          break;
        case Mode::Land:
          tz = d.z > 0.05 ? -0.8 : 0.0;
          break;
        case Mode::ReturnHome: {
          const double dist = std::hypot(d.x, d.y);
          if (dist > 0.25) {
            tx = -d.x / std::max(0.001, dist) * 1.5;
            ty = -d.y / std::max(0.001, dist) * 1.5;
          } else {
            tz = d.z > 0.05 ? -0.8 : 0.0;
          }
          break;
        }
        case Mode::Hold:
        case Mode::EmergencyStop:
          break;
      }

      constexpr double accel = 4.0;
      d.vx = approach(d.vx, tx, accel * dt);
      d.vy = approach(d.vy, ty, accel * dt);
      d.vz = approach(d.vz, tz, accel * dt);
      d.yaw_rate = approach(d.yaw_rate, tyaw, 2.0 * dt);

      d.x += d.vx * dt;
      d.y += d.vy * dt;
      d.z = std::max(0.0, d.z + d.vz * dt);
      if (d.z <= 0.0 && d.vz < 0.0) d.vz = 0.0;
      d.yaw += d.yaw_rate * dt;

      const double activity =
          std::hypot(d.vx, d.vy) + std::abs(d.vz) +
          (d.z > 0.05 ? 0.5 : 0.0);
      d.battery =
          std::max(0.0, d.battery - dt * (0.002 + 0.004 * activity));

      geometry_msgs::msg::Twist twist;
      twist.linear.x = d.vx;
      twist.linear.y = d.vy;
      twist.linear.z = d.vz;
      twist.angular.z = d.yaw_rate;
      d.gazebo_cmd_pub->publish(twist);

      drone_system_interfaces::msg::DroneTelemetry t;
      t.drone_id = d.id;
      t.sequence = ++d.telemetry_seq;
      t.stamp = now();
      t.position.x = d.x;
      t.position.y = d.y;
      t.position.z = d.z;
      t.velocity.x = d.vx;
      t.velocity.y = d.vy;
      t.velocity.z = d.vz;
      t.yaw_rad = d.yaw;
      t.battery_pct = static_cast<float>(d.battery);
      t.link_quality_pct = static_cast<float>(
          std::clamp(
              100.0 - hb_age.count() * 100.0 /
                  std::max<std::int64_t>(
                      1, static_cast<std::int64_t>(
                             limits_.land_after.count())),
              0.0, 100.0));
      t.peer_count = peer_count;
      t.cpu_pct = 0.0F;
      t.memory_mb = 0.0F;
      t.mode = static_cast<std::uint8_t>(sp.mode);
      t.armed = d.z > 0.01 || sp.mode == Mode::Takeoff;
      t.failsafe_active = d.brain.failsafe_active();
      t.failsafe_reason = d.brain.failsafe_reason();
      t.manager_heartbeat_age_ms =
          static_cast<std::uint32_t>(
              std::clamp<std::int64_t>(
                  static_cast<std::int64_t>(hb_age.count()),
                  0, 0xffffffffLL));
      telemetry_pub_->publish(t);
    }
  }

  int max_drones_{64};
  int step_ms_{20};
  int peer_timeout_ms_{1500};
  bool drop_manager_link_{false};
  bool drop_peer_link_{false};
  drone_system_core::Limits limits_;
  StateMachine::Clock::time_point manager_last_seen_;
  StateMachine::Clock::time_point peer_guard_started_;
  std::vector<Drone> drones_;
  std::unordered_map<std::string, StateMachine::Clock::time_point>
      peer_last_seen_;

  rclcpp::Publisher<
      drone_system_interfaces::msg::DroneTelemetry>::SharedPtr
      telemetry_pub_;
  rclcpp::Publisher<
      drone_system_interfaces::msg::Heartbeat>::SharedPtr
      peer_heartbeat_pub_;
  rclcpp::Subscription<
      drone_system_interfaces::msg::FleetCommand>::SharedPtr
      command_sub_;
  rclcpp::Subscription<
      drone_system_interfaces::msg::Heartbeat>::SharedPtr
      manager_heartbeat_sub_;
  rclcpp::Subscription<
      drone_system_interfaces::msg::Heartbeat>::SharedPtr
      peer_heartbeat_sub_;
  rclcpp::TimerBase::SharedPtr peer_timer_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(std::make_shared<FleetSimulator>());
  executor.spin();
  rclcpp::shutdown();
  return 0;
}
