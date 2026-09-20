#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include <rclcpp/rclcpp.hpp>
#include <drone_system_interfaces/msg/drone_telemetry.hpp>
#include <drone_system_interfaces/msg/fleet_command.hpp>

class FleetConsole final : public QMainWindow {
 public:
  FleetConsole() {
    node_ = std::make_shared<rclcpp::Node>("fleet_console");
    command_pub_ = node_->create_publisher<drone_system_interfaces::msg::FleetCommand>(
      "/fleet/command", rclcpp::QoS(32).reliable());
    telemetry_sub_ = node_->create_subscription<drone_system_interfaces::msg::DroneTelemetry>(
      "/fleet/telemetry", rclcpp::QoS(8).best_effort(),
      [this](drone_system_interfaces::msg::DroneTelemetry::ConstSharedPtr m) { update_row(*m); });

    auto* root = new QWidget;
    auto* layout = new QVBoxLayout(root);
    status_ = new QLabel("Fleet console — waiting for telemetry");
    table_ = new QTableWidget(0, 10);
    table_->setHorizontalHeaderLabels({"Drone","Mode","X","Y","Z","Battery","Link","Peers","Failsafe","HB age ms"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);

    auto* controls = new QHBoxLayout;
    target_ = new QComboBox;
    target_->addItem("*");
    controls->addWidget(new QLabel("Target"));
    controls->addWidget(target_);
    add_button(controls, "Hold", 0);
    add_button(controls, "Takeoff 3m", 1);
    add_button(controls, "Land", 2);
    add_button(controls, "Return Home", 3);
    add_button(controls, "E-Stop", 5);

    layout->addWidget(status_);
    layout->addWidget(table_);
    layout->addLayout(controls);
    setCentralWidget(root);
    resize(1400, 650);
    setWindowTitle("Drone System — Fleet Console");

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this] { rclcpp::spin_some(node_); });
    timer_->start(20);
  }

 private:
  void add_button(QHBoxLayout* layout, const QString& label, std::uint8_t mode) {
    auto* b = new QPushButton(label);
    connect(b, &QPushButton::clicked, this, [this, mode] { send(mode); });
    layout->addWidget(b);
  }

  void send(std::uint8_t mode) {
    drone_system_interfaces::msg::FleetCommand c;
    c.drone_id = target_->currentText().toStdString();
    c.sequence = ++sequence_;
    c.issued_at = node_->now();
    c.ttl_ms = 1000;
    c.mode = mode;
    c.takeoff_altitude_m = mode == 1 ? 3.0 : 0.0;
    command_pub_->publish(c);
  }

  void update_row(const drone_system_interfaces::msg::DroneTelemetry& t) {
    int row;
    auto it = rows_.find(t.drone_id);
    if (it == rows_.end()) {
      row = table_->rowCount();
      table_->insertRow(row);
      rows_[t.drone_id] = row;
      target_->addItem(QString::fromStdString(t.drone_id));
    } else {
      row = it->second;
    }
    const QStringList vals{
      QString::fromStdString(t.drone_id),
      QString::number(t.mode),
      QString::number(t.position.x, 'f', 2),
      QString::number(t.position.y, 'f', 2),
      QString::number(t.position.z, 'f', 2),
      QString::number(t.battery_pct, 'f', 1) + "%",
      QString::number(t.link_quality_pct, 'f', 0) + "%",
      QString::number(t.peer_count),
      t.failsafe_active ? QString::fromStdString(t.failsafe_reason) : "OK",
      QString::number(t.manager_heartbeat_age_ms)
    };
    for (int col = 0; col < vals.size(); ++col) {
      auto* item = table_->item(row, col);
      if (!item) { item = new QTableWidgetItem; table_->setItem(row, col, item); }
      item->setText(vals[col]);
    }
    status_->setText(QString("Active drones: %1").arg(rows_.size()));
  }

  std::uint64_t sequence_{0};
  std::shared_ptr<rclcpp::Node> node_;
  rclcpp::Publisher<drone_system_interfaces::msg::FleetCommand>::SharedPtr command_pub_;
  rclcpp::Subscription<drone_system_interfaces::msg::DroneTelemetry>::SharedPtr telemetry_sub_;
  QTableWidget* table_{};
  QComboBox* target_{};
  QLabel* status_{};
  QTimer* timer_{};
  std::unordered_map<std::string, int> rows_;
};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  QApplication app(argc, argv);
  FleetConsole window;
  window.show();
  const int code = app.exec();
  rclcpp::shutdown();
  return code;
}
