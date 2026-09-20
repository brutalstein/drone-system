#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include <rclcpp/rclcpp.hpp>
#include <drone_system_interfaces/msg/drone_telemetry.hpp>
#include <drone_system_interfaces/msg/heartbeat.hpp>

using namespace std::chrono_literals;

class FleetManager final : public rclcpp::Node {
 public:
  FleetManager() : Node("fleet_manager") {
    max_drones_ = declare_parameter<int>("max_drones", 64);
    stale_ms_ = declare_parameter<int>("stale_telemetry_ms", 1500);
    const double heartbeat_hz = declare_parameter<double>("manager_heartbeat_hz", 5.0);

    auto reliable = rclcpp::QoS(rclcpp::KeepLast(16)).reliable();
    auto telemetry_qos = rclcpp::QoS(rclcpp::KeepLast(8)).best_effort();

    hb_pub_ = create_publisher<drone_system_interfaces::msg::Heartbeat>(
        "/fleet/manager_heartbeat", reliable);
    telemetry_sub_ = create_subscription<drone_system_interfaces::msg::DroneTelemetry>(
        "/fleet/telemetry", telemetry_qos,
        [this](drone_system_interfaces::msg::DroneTelemetry::ConstSharedPtr msg) {
          std::scoped_lock lock(mu_);
          if (last_seen_.contains(msg->drone_id) ||
              static_cast<int>(last_seen_.size()) < max_drones_) {
            last_seen_[msg->drone_id] = std::chrono::steady_clock::now();
          }
        });

    const auto period = std::chrono::duration<double>(1.0 / std::max(1.0, heartbeat_hz));
    hb_timer_ = create_wall_timer(
        std::chrono::duration_cast<std::chrono::milliseconds>(period),
        [this] { publish_heartbeat(); });
    gc_timer_ = create_wall_timer(500ms, [this] { gc(); });
  }

 private:
  void publish_heartbeat() {
    drone_system_interfaces::msg::Heartbeat hb;
    hb.source_id = "fleet_manager";
    hb.sequence = ++sequence_;
    hb.stamp = now();
    hb_pub_->publish(hb);
  }

  void gc() {
    const auto now_steady = std::chrono::steady_clock::now();
    std::scoped_lock lock(mu_);
    for (auto it = last_seen_.begin(); it != last_seen_.end();) {
      if (now_steady - it->second > std::chrono::milliseconds(stale_ms_ * 4)) {
        it = last_seen_.erase(it);
      } else {
        ++it;
      }
    }
  }

  int max_drones_{64};
  int stale_ms_{1500};
  std::uint64_t sequence_{0};
  std::mutex mu_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_seen_;
  rclcpp::Publisher<drone_system_interfaces::msg::Heartbeat>::SharedPtr hb_pub_;
  rclcpp::Subscription<drone_system_interfaces::msg::DroneTelemetry>::SharedPtr telemetry_sub_;
  rclcpp::TimerBase::SharedPtr hb_timer_;
  rclcpp::TimerBase::SharedPtr gc_timer_;
};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(std::make_shared<FleetManager>());
  executor.spin();
  rclcpp::shutdown();
  return 0;
}
