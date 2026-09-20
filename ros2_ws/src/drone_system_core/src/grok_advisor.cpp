#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <drone_system_interfaces/msg/drone_telemetry.hpp>

class GrokAdvisor final : public rclcpp::Node {
 public:
  GrokAdvisor() : Node("grok_advisor") {
    const char* key = std::getenv("XAI_API_KEY");
    api_key_ = key ? key : "";
    const char* model = std::getenv("DRONE_GROK_MODEL");
    model_ = model ? model : "grok-4.6";

    answer_pub_ = create_publisher<std_msgs::msg::String>("/fleet/ai_answer", 4);
    telemetry_sub_ = create_subscription<drone_system_interfaces::msg::DroneTelemetry>(
      "/fleet/telemetry", rclcpp::QoS(8).best_effort(),
      [this](drone_system_interfaces::msg::DroneTelemetry::ConstSharedPtr m) {
        std::scoped_lock lock(mu_);
        latest_[m->drone_id] = *m;
      });
    query_sub_ = create_subscription<std_msgs::msg::String>(
      "/fleet/ai_query", 4,
      [this](std_msgs::msg::String::ConstSharedPtr m) {
        std::scoped_lock lock(mu_);
        if (queue_.size() >= 4) queue_.pop_front();
        queue_.push_back(m->data.substr(0, 2000));
        cv_.notify_one();
      });

    curl_global_init(CURL_GLOBAL_DEFAULT);
    worker_ = std::thread([this] { worker_loop(); });
  }

  ~GrokAdvisor() override {
    stop_.store(true);
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    curl_global_cleanup();
  }

 private:
  static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
  }

  std::string snapshot() {
    std::scoped_lock lock(mu_);
    std::ostringstream s;
    for (const auto& [id, t] : latest_) {
      s << id << " pos=(" << t.position.x << "," << t.position.y << "," << t.position.z
        << ") vel=(" << t.velocity.x << "," << t.velocity.y << "," << t.velocity.z
        << ") battery=" << t.battery_pct << "% link=" << t.link_quality_pct
        << "% mode=" << static_cast<int>(t.mode)
        << " failsafe=" << (t.failsafe_active ? t.failsafe_reason : "none") << "\n";
    }
    return s.str().substr(0, 12000);
  }

  std::string call_grok(const std::string& query) {
    if (api_key_.empty()) return "XAI_API_KEY is not configured.";

    nlohmann::json body = {
      {"model", model_},
      {"messages", nlohmann::json::array({
        {{"role","system"},{"content",
          "You are an advisory assistant for a civilian drone simulator. Explain telemetry, diagnostics and safe operator actions. Never claim to control the aircraft."}},
        {{"role","user"},{"content","Fleet snapshot:\n" + snapshot() + "\nQuestion:\n" + query}}
      })},
      {"temperature", 0.1},
      {"max_tokens", 700}
    };

    CURL* curl = curl_easy_init();
    if (!curl) return "Unable to initialize HTTP client.";
    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    const std::string auth = "Authorization: Bearer " + api_key_;
    headers = curl_slist_append(headers, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.x.ai/v1/chat/completions");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    const std::string payload = body.dump();
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 8000L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2500L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &GrokAdvisor::write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "drone-system/0.1");
    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK || status < 200 || status >= 300)
      return "Grok request failed (HTTP " + std::to_string(status) + ").";

    try {
      const auto j = nlohmann::json::parse(response);
      return j.at("choices").at(0).at("message").at("content").get<std::string>();
    } catch (...) {
      return "Grok returned an unexpected response.";
    }
  }

  void worker_loop() {
    while (!stop_.load()) {
      std::string q;
      {
        std::unique_lock lock(mu_);
        cv_.wait(lock, [this] { return stop_.load() || !queue_.empty(); });
        if (stop_.load()) break;
        q = std::move(queue_.front());
        queue_.pop_front();
      }
      std_msgs::msg::String out;
      out.data = call_grok(q);
      answer_pub_->publish(out);
    }
  }

  std::string api_key_, model_;
  std::atomic_bool stop_{false};
  std::thread worker_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::string> queue_;
  std::unordered_map<std::string, drone_system_interfaces::msg::DroneTelemetry> latest_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr answer_pub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr query_sub_;
  rclcpp::Subscription<drone_system_interfaces::msg::DroneTelemetry>::SharedPtr telemetry_sub_;
};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<GrokAdvisor>());
  rclcpp::shutdown();
  return 0;
}
