#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <drone_system_interfaces/msg/drone_telemetry.hpp>
#include <drone_system_interfaces/msg/fleet_command.hpp>

namespace {
using Telemetry = drone_system_interfaces::msg::DroneTelemetry;
using FleetCommand = drone_system_interfaces::msg::FleetCommand;

QString mode_name(std::uint8_t mode) {
  switch (mode) {
    case FleetCommand::HOLD: return "HOLD";
    case FleetCommand::TAKEOFF: return "TAKEOFF";
    case FleetCommand::LAND: return "LAND";
    case FleetCommand::RETURN_HOME: return "RETURN";
    case FleetCommand::VELOCITY: return "VELOCITY";
    case FleetCommand::EMERGENCY_STOP: return "E-STOP";
    case FleetCommand::GOTO_POSITION: return "GOTO";
    default: return "UNKNOWN";
  }
}

QFrame* make_card() {
  auto* frame = new QFrame;
  frame->setProperty("card", true);
  return frame;
}

struct RadarPoint {
  QString id;
  QPointF position;
  double battery{0.0};
  bool failsafe{false};
  bool selected{false};
};

class FleetRadar final : public QWidget {
 public:
  explicit FleetRadar(QWidget* parent = nullptr) : QWidget(parent), empty_(":/assets/empty_fleet.svg") {
    setMinimumHeight(315);
  }
  void set_points(std::vector<RadarPoint> points) { points_ = std::move(points); update(); }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor("#0A1725"));
    if (points_.empty()) {
      if (!empty_.isNull()) {
        const auto scaled = empty_.scaled(size() * 0.82, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.setOpacity(0.78);
        p.drawPixmap((width() - scaled.width()) / 2, (height() - scaled.height()) / 2, scaled);
      }
      return;
    }
    QRectF area = rect().adjusted(26, 24, -26, -28);
    const QPointF center = area.center();
    double range = 10.0;
    for (const auto& d : points_) {
      range = std::max(range, std::abs(d.position.x()) + 2.0);
      range = std::max(range, std::abs(d.position.y()) + 2.0);
    }
    const double radius = std::min(area.width(), area.height()) * 0.45;
    const double scale = radius / range;
    p.setPen(QPen(QColor("#183049"), 1));
    for (int ring = 1; ring <= 4; ++ring) {
      const double r = radius * ring / 4.0;
      p.drawEllipse(center, r, r);
    }
    p.setPen(QPen(QColor("#203B54"), 1, Qt::DashLine));
    p.drawLine(QPointF(center.x() - radius, center.y()), QPointF(center.x() + radius, center.y()));
    p.drawLine(QPointF(center.x(), center.y() - radius), QPointF(center.x(), center.y() + radius));
    p.setPen(QColor("#53708B"));
    p.drawText(QRectF(area.left(), area.top(), area.width(), 20), Qt::AlignRight,
               QString("AUTO SCALE ±%1 m").arg(range, 0, 'f', 0));

    for (const auto& d : points_) {
      const QPointF pt(center.x() + d.position.x() * scale, center.y() - d.position.y() * scale);
      const QColor accent = d.failsafe ? QColor("#FF5576") : (d.selected ? QColor("#F0E8FF") : QColor("#19D5F1"));
      const QColor secondary = d.failsafe ? QColor("#8E2F47") : QColor("#756BFF");
      p.setPen(QPen(secondary, d.selected ? 3 : 2));
      p.setBrush(QColor(secondary.red(), secondary.green(), secondary.blue(), 38));
      p.drawEllipse(pt, d.selected ? 17 : 14, d.selected ? 17 : 14);
      p.setPen(QPen(accent, 3, Qt::SolidLine, Qt::RoundCap));
      p.drawLine(pt + QPointF(-9, 0), pt + QPointF(9, 0));
      p.drawLine(pt + QPointF(0, -9), pt + QPointF(0, 9));
      p.setBrush(accent); p.setPen(Qt::NoPen); p.drawEllipse(pt, 4.5, 4.5);
      p.setPen(QColor("#D8E9F7"));
      p.drawText(QRectF(pt.x() + 18, pt.y() - 13, 120, 18), Qt::AlignLeft | Qt::AlignVCenter, d.id);
      p.setPen(QColor("#6E8AA4"));
      p.drawText(QRectF(pt.x() + 18, pt.y() + 3, 100, 16), Qt::AlignLeft | Qt::AlignVCenter,
                 QString("%1%").arg(d.battery, 0, 'f', 0));
    }
  }
 private:
  std::vector<RadarPoint> points_;
  QPixmap empty_;
};

class MetricCard final : public QFrame {
 public:
  MetricCard(const QString& name, const QString& hint, QWidget* parent = nullptr) : QFrame(parent) {
    setProperty("card", true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 13, 16, 13); layout->setSpacing(3);
    auto* n = new QLabel(name); n->setObjectName("metricName");
    value_ = new QLabel("--"); value_->setObjectName("metricValue");
    hint_ = new QLabel(hint); hint_->setObjectName("metricHint");
    layout->addWidget(n); layout->addWidget(value_); layout->addWidget(hint_);
  }
  void set_value(const QString& value, const QString& hint = {}) {
    value_->setText(value); if (!hint.isEmpty()) hint_->setText(hint);
  }
 private:
  QLabel* value_{};
  QLabel* hint_{};
};

struct FleetEntry {
  Telemetry telemetry;
  std::chrono::steady_clock::time_point last_seen{};
  int row{-1};
  QProgressBar* battery_bar{};
  QProgressBar* link_bar{};
};
}  // namespace

class FleetConsole final : public QMainWindow {
 public:
  FleetConsole() {
    node_ = std::make_shared<rclcpp::Node>("fleet_console");
    command_pub_ = node_->create_publisher<FleetCommand>("/fleet/command", rclcpp::QoS(32).reliable());
    ai_query_pub_ = node_->create_publisher<std_msgs::msg::String>("/fleet/ai_query", 4);
    telemetry_sub_ = node_->create_subscription<Telemetry>(
      "/fleet/telemetry", rclcpp::QoS(8).best_effort(),
      [this](Telemetry::ConstSharedPtr msg) { on_telemetry(*msg); });
    ai_answer_sub_ = node_->create_subscription<std_msgs::msg::String>(
      "/fleet/ai_answer", 4,
      [this](std_msgs::msg::String::ConstSharedPtr msg) {
        ai_answer_->setMarkdown(QString::fromStdString(msg->data));
        log_event("AI advisor response received", "#7A8FA6");
      });

    build_ui();
    ros_timer_ = new QTimer(this);
    connect(ros_timer_, &QTimer::timeout, this, [this] { rclcpp::spin_some(node_); });
    ros_timer_->start(10);
    ui_timer_ = new QTimer(this);
    connect(ui_timer_, &QTimer::timeout, this, [this] { refresh_health(); });
    ui_timer_->start(250);

    manual_timer_ = new QTimer(this);
    connect(manual_timer_, &QTimer::timeout, this, [this] {
      if (manual_active_) {
        send_velocity(manual_vx_, manual_vy_, manual_vz_, manual_yaw_, false);
      }
    });
    manual_timer_->start(100);
    setWindowTitle("AERION Fleet Operations");
    resize(1720, 980);
    setMinimumSize(1280, 760);
    log_event("Operations console initialized", "#50E3C2");
  }

 protected:
  void keyPressEvent(QKeyEvent* event) override {
    if (event->isAutoRepeat() || text_input_focused()) { QMainWindow::keyPressEvent(event); return; }
    switch (event->key()) {
      case Qt::Key_W: begin_manual(horizontal_speed_->value(),0,0,0); break;
      case Qt::Key_S: begin_manual(-horizontal_speed_->value(),0,0,0); break;
      case Qt::Key_A: begin_manual(0,horizontal_speed_->value(),0,0); break;
      case Qt::Key_D: begin_manual(0,-horizontal_speed_->value(),0,0); break;
      case Qt::Key_R: begin_manual(0,0,vertical_speed_->value(),0); break;
      case Qt::Key_F: begin_manual(0,0,-vertical_speed_->value(),0); break;
      case Qt::Key_Q: begin_manual(0,0,0,yaw_rate_->value()); break;
      case Qt::Key_E: begin_manual(0,0,0,-yaw_rate_->value()); break;
      case Qt::Key_Space: end_manual(); send_simple(FleetCommand::HOLD, "HOLD"); break;
      default: QMainWindow::keyPressEvent(event); break;
    }
  }
  void keyReleaseEvent(QKeyEvent* event) override {
    if (!event->isAutoRepeat() && !text_input_focused()) {
      switch (event->key()) {
        case Qt::Key_W: case Qt::Key_S: case Qt::Key_A: case Qt::Key_D:
        case Qt::Key_R: case Qt::Key_F: case Qt::Key_Q: case Qt::Key_E:
          end_manual(); return;
        default: break;
      }
    }
    QMainWindow::keyReleaseEvent(event);
  }

 private:
  void add_section(const QString& title, const QString& sub, QVBoxLayout* layout) {
    auto* t = new QLabel(title); t->setObjectName("sectionTitle");
    auto* s = new QLabel(sub); s->setObjectName("sectionSub");
    layout->addWidget(t); layout->addWidget(s);
  }

  void build_ui() {
    auto* root = new QWidget; root->setObjectName("appRoot");
    auto* outer = new QHBoxLayout(root); outer->setContentsMargins(0,0,0,0); outer->setSpacing(0);

    auto* sidebar = new QFrame; sidebar->setProperty("sidebar", true); sidebar->setFixedWidth(224);
    auto* side = new QVBoxLayout(sidebar); side->setContentsMargins(18,22,18,18); side->setSpacing(8);
    auto* brand_row = new QHBoxLayout;
    auto* mark = new QLabel; QPixmap logo(":/assets/brand_mark.svg");
    mark->setPixmap(logo.scaled(48,48,Qt::KeepAspectRatio,Qt::SmoothTransformation)); mark->setFixedSize(50,50);
    auto* brand_copy = new QVBoxLayout;
    auto* brand = new QLabel("AERION"); brand->setObjectName("brandTitle");
    auto* brand_sub = new QLabel("FLEET OS"); brand_sub->setObjectName("brandSub");
    brand_copy->addStretch(); brand_copy->addWidget(brand); brand_copy->addWidget(brand_sub); brand_copy->addStretch();
    brand_row->addWidget(mark); brand_row->addLayout(brand_copy); brand_row->addStretch();
    side->addLayout(brand_row); side->addSpacing(24);
    const QStringList nav{"Operations","Flight Control","AI Advisor","System Health"};
    for (int i=0;i<nav.size();++i) {
      auto* label = new QLabel((i==0 ? "●  " : "○  ") + nav[i]);
      label->setProperty(i==0 ? "navActive" : "nav", true); side->addWidget(label);
    }
    side->addStretch();
    auto* safety = new QFrame; safety->setProperty("softCard",true);
    auto* safety_layout = new QVBoxLayout(safety); safety_layout->setContentsMargins(12,11,12,11);
    auto* safety_title = new QLabel("CONTROL ENVELOPE"); safety_title->setObjectName("sectionSub");
    auto* safety_body = new QLabel("8 m/s horizontal\n3 m/s vertical\n1.2 rad/s yaw"); safety_body->setProperty("muted",true);
    safety_layout->addWidget(safety_title); safety_layout->addWidget(safety_body); side->addWidget(safety);
    auto* mesh = new QLabel;
    QPixmap mesh_pix(":/assets/fleet_mesh.svg");
    mesh->setPixmap(mesh_pix.scaled(188,72,Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation));
    mesh->setFixedHeight(72);
    mesh->setAlignment(Qt::AlignCenter);
    mesh->setObjectName("meshVisual");
    side->addWidget(mesh);
    auto* stack = new QLabel("ROS 2 Jazzy\nGazebo Harmonic • C++20"); stack->setProperty("muted",true); side->addWidget(stack);

    auto* body_widget = new QWidget;
    auto* body = new QVBoxLayout(body_widget); body->setContentsMargins(24,20,24,20); body->setSpacing(14);
    auto* header = new QHBoxLayout; auto* title_col = new QVBoxLayout;
    auto* title = new QLabel("Fleet Operations"); title->setObjectName("pageTitle");
    auto* subtitle = new QLabel("Live telemetry, deterministic command routing and multi-drone supervision"); subtitle->setObjectName("pageSub");
    title_col->addWidget(title); title_col->addWidget(subtitle); header->addLayout(title_col); header->addStretch();
    target_ = new QComboBox; target_->setMinimumWidth(190); target_->addItem("All drones","*");
    header->addWidget(new QLabel("CONTROL TARGET")); header->addWidget(target_);
    connection_ = new QLabel("●  WAITING"); connection_->setObjectName("statusPill"); header->addWidget(connection_);
    body->addLayout(header);

    auto* metrics = new QHBoxLayout;
    active_card_=new MetricCard("ACTIVE DRONES","telemetry < 2s");
    battery_card_=new MetricCard("FLEET BATTERY","average");
    link_card_=new MetricCard("LINK HEALTH","manager channel");
    failsafe_card_=new MetricCard("FAILSAFES","active now");
    metrics->addWidget(active_card_); metrics->addWidget(battery_card_); metrics->addWidget(link_card_); metrics->addWidget(failsafe_card_);
    body->addLayout(metrics);

    auto* top = new QSplitter(Qt::Horizontal); top->setChildrenCollapsible(false);
    auto* radar_card=make_card(); auto* radar_layout=new QVBoxLayout(radar_card); radar_layout->setContentsMargins(14,14,14,14);
    add_section("Live Fleet Radar","Auto-scaled XY position view • selection follows control target",radar_layout);
    radar_=new FleetRadar; radar_layout->addWidget(radar_,1); top->addWidget(radar_card);

    auto* telemetry_card=make_card(); auto* telemetry_layout=new QVBoxLayout(telemetry_card); telemetry_layout->setContentsMargins(14,14,14,14);
    add_section("Drone Telemetry","High-rate bounded stream • dynamic fleet discovery",telemetry_layout);
    table_=new QTableWidget(0,10);
    table_->setHorizontalHeaderLabels({"Drone","Mode","Position","Velocity","Battery","Link","Peers","Armed","Failsafe","HB age"});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false); table_->setShowGrid(false); table_->setAlternatingRowColors(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection); table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) {
      auto* item = table_->item(row, 0);
      if (!item) return;
      const int idx = target_->findData(item->text());
      if (idx >= 0) {
        target_->setCurrentIndex(idx);
        log_event(QString("Control target selected: %1").arg(item->text()), "#7DE7FF");
      }
    });
    telemetry_layout->addWidget(table_); top->addWidget(telemetry_card); top->setStretchFactor(0,5); top->setStretchFactor(1,7);
    body->addWidget(top,5);

    auto* bottom=new QSplitter(Qt::Horizontal); bottom->setChildrenCollapsible(false);
    auto* control_card=make_card(); auto* control=new QVBoxLayout(control_card); control->setContentsMargins(14,14,14,14); control->setSpacing(8);
    add_section("Flight Control","Press-and-hold movement • release returns to HOLD",control);
    auto* tuning=new QGridLayout;
    horizontal_speed_=spin(0.1,8.0,1.5," m/s"); vertical_speed_=spin(0.1,3.0,1.0," m/s");
    yaw_rate_=spin(0.1,1.2,0.5," rad/s"); takeoff_alt_=spin(0.2,30.0,3.0," m");
    tuning->addWidget(new QLabel("Horizontal"),0,0); tuning->addWidget(horizontal_speed_,0,1);
    tuning->addWidget(new QLabel("Vertical"),0,2); tuning->addWidget(vertical_speed_,0,3);
    tuning->addWidget(new QLabel("Yaw"),1,0); tuning->addWidget(yaw_rate_,1,1);
    tuning->addWidget(new QLabel("Takeoff"),1,2); tuning->addWidget(takeoff_alt_,1,3); control->addLayout(tuning);

    auto* actions=new QHBoxLayout;
    auto* takeoff=button("Takeoff",true);
    connect(takeoff,&QPushButton::clicked,this,[this]{ FleetCommand c=base_command(FleetCommand::TAKEOFF); c.takeoff_altitude_m=takeoff_alt_->value(); publish(c,QString("TAKEOFF %1 m").arg(takeoff_alt_->value(),0,'f',1)); });
    auto* land=button("Land"); connect(land,&QPushButton::clicked,this,[this]{send_simple(FleetCommand::LAND,"LAND");});
    auto* rtl=button("Return Home"); connect(rtl,&QPushButton::clicked,this,[this]{send_simple(FleetCommand::RETURN_HOME,"RETURN_HOME");});
    auto* hold=button("Hold"); hold->setProperty("hold",true); connect(hold,&QPushButton::clicked,this,[this]{send_simple(FleetCommand::HOLD,"HOLD");});
    actions->addWidget(takeoff); actions->addWidget(land); actions->addWidget(rtl); actions->addWidget(hold); control->addLayout(actions);

    auto* precision = new QFrame; precision->setProperty("controlGroup", true);
    auto* precision_layout = new QVBoxLayout(precision); precision_layout->setContentsMargins(12,10,12,10); precision_layout->setSpacing(7);
    auto* precision_title = new QLabel("PRECISION TARGET"); precision_title->setObjectName("controlGroupTitle");
    auto* precision_hint = new QLabel("Absolute simulator coordinates • bounded speed • one or all drones"); precision_hint->setProperty("muted",true);
    precision_layout->addWidget(precision_title); precision_layout->addWidget(precision_hint);
    auto* goto_grid = new QGridLayout;
    goto_x_=spin(-500.0,500.0,0.0," m"); goto_y_=spin(-500.0,500.0,0.0," m");
    goto_z_=spin(0.0,30.0,3.0," m"); goto_speed_=spin(0.1,8.0,2.0," m/s");
    goto_grid->addWidget(new QLabel("X"),0,0); goto_grid->addWidget(goto_x_,0,1);
    goto_grid->addWidget(new QLabel("Y"),0,2); goto_grid->addWidget(goto_y_,0,3);
    goto_grid->addWidget(new QLabel("Z"),1,0); goto_grid->addWidget(goto_z_,1,1);
    goto_grid->addWidget(new QLabel("Max speed"),1,2); goto_grid->addWidget(goto_speed_,1,3);
    auto* goto_button = button("Send Position Target", true);
    connect(goto_button,&QPushButton::clicked,this,[this]{send_goto(goto_x_->value(),goto_y_->value(),goto_z_->value(),goto_speed_->value());});
    precision_layout->addLayout(goto_grid); precision_layout->addWidget(goto_button);
    control->addWidget(precision);

    auto* pad=new QGridLayout;
    auto* forward=button("↑  Forward"); auto* left=button("←  Left"); auto* stop=button("■  HOLD"); stop->setProperty("hold",true);
    auto* right=button("Right  →"); auto* back=button("↓  Back");
    bind_momentary(forward,[this]{begin_manual(horizontal_speed_->value(),0,0,0);});
    bind_momentary(back,[this]{begin_manual(-horizontal_speed_->value(),0,0,0);});
    bind_momentary(left,[this]{begin_manual(0,horizontal_speed_->value(),0,0);});
    bind_momentary(right,[this]{begin_manual(0,-horizontal_speed_->value(),0,0);});
    connect(stop,&QPushButton::clicked,this,[this]{send_simple(FleetCommand::HOLD,"HOLD");});
    pad->addWidget(forward,0,1); pad->addWidget(left,1,0); pad->addWidget(stop,1,1); pad->addWidget(right,1,2); pad->addWidget(back,2,1);
    auto* yaw_left=button("Q  ↺ Yaw"); auto* up=button("R  +Altitude"); auto* down=button("F  -Altitude"); auto* yaw_right=button("Yaw ↻  E");
    bind_momentary(yaw_left,[this]{begin_manual(0,0,0,yaw_rate_->value());});
    bind_momentary(yaw_right,[this]{begin_manual(0,0,0,-yaw_rate_->value());});
    bind_momentary(up,[this]{begin_manual(0,0,vertical_speed_->value(),0);});
    bind_momentary(down,[this]{begin_manual(0,0,-vertical_speed_->value(),0);});
    pad->addWidget(yaw_left,0,0); pad->addWidget(up,0,2); pad->addWidget(down,2,0); pad->addWidget(yaw_right,2,2); control->addLayout(pad);

    auto* emergency=button("EMERGENCY STOP"); emergency->setProperty("danger",true);
    connect(emergency,&QPushButton::clicked,this,[this]{
      auto choice=QMessageBox::question(this,"Emergency stop","Send EMERGENCY STOP to the selected target?",QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
      if(choice==QMessageBox::Yes) send_simple(FleetCommand::EMERGENCY_STOP,"EMERGENCY_STOP");
    });
    control->addWidget(emergency);
    command_line_=new QLineEdit; command_line_->setPlaceholderText("Command palette: goto 10 -4 6 2 | takeoff 5 | vel 1 0 0 0.2 | land | rtl | hold | stop");
    connect(command_line_,&QLineEdit::returnPressed,this,[this]{parse_command();}); control->addWidget(command_line_);
    auto* shortcut=new QLabel("Keyboard: W/A/S/D move • R/F altitude • Q/E yaw • Space hold"); shortcut->setProperty("muted",true); control->addWidget(shortcut);
    bottom->addWidget(control_card);

    auto* ai_card=make_card(); auto* ai=new QVBoxLayout(ai_card); ai->setContentsMargins(14,14,14,14); ai->setSpacing(8);
    add_section("Grok Fleet Advisor","Read-only advisory layer • never publishes flight commands",ai);
    ai_answer_=new QTextBrowser;
    ai_answer_->setMarkdown("**Advisor ready.** Ask about telemetry, anomalies, failsafes or fleet health.\n\nRequires XAI_API_KEY in the launcher environment.");
    ai->addWidget(ai_answer_,1);
    auto* ai_row=new QHBoxLayout; ai_query_=new QLineEdit; ai_query_->setPlaceholderText("Why is drone_2 in failsafe?");
    auto* ask=button("Ask Grok",true); connect(ask,&QPushButton::clicked,this,[this]{ask_ai();}); connect(ai_query_,&QLineEdit::returnPressed,this,[this]{ask_ai();});
    ai_row->addWidget(ai_query_,1); ai_row->addWidget(ask); ai->addLayout(ai_row); bottom->addWidget(ai_card);

    auto* event_card=make_card(); auto* events=new QVBoxLayout(event_card); events->setContentsMargins(14,14,14,14);
    add_section("Operations Log","Local command and system activity",events); event_log_=new QTextBrowser; events->addWidget(event_log_); bottom->addWidget(event_card);
    bottom->setStretchFactor(0,5); bottom->setStretchFactor(1,4); bottom->setStretchFactor(2,3); body->addWidget(bottom,4);

    outer->addWidget(sidebar); outer->addWidget(body_widget,1); setCentralWidget(root);
  }

  QDoubleSpinBox* spin(double min,double max,double value,const QString& suffix) {
    auto* box=new QDoubleSpinBox; box->setRange(min,max); box->setDecimals(2); box->setSingleStep(0.1); box->setValue(value); box->setSuffix(suffix); return box;
  }
  QPushButton* button(const QString& text,bool primary=false) {
    auto* b=new QPushButton(text); if(primary)b->setProperty("primary",true); b->setMinimumHeight(38); return b;
  }
  template<typename Fn> void bind_momentary(QPushButton* b,Fn fn) {
    connect(b,&QPushButton::pressed,this,fn);
    connect(b,&QPushButton::released,this,[this]{end_manual();});
  }
  bool text_input_focused() const { return (command_line_&&command_line_->hasFocus())||(ai_query_&&ai_query_->hasFocus()); }
  QString selected_target() const { auto data=target_->currentData(); return data.isValid()?data.toString():"*"; }

  FleetCommand base_command(std::uint8_t mode) {
    FleetCommand c; c.drone_id=selected_target().toStdString(); c.sequence=++sequence_; c.issued_at=node_->now(); c.ttl_ms=1000; c.mode=mode; return c;
  }
  void publish(const FleetCommand& c,const QString& label) {
    if (c.mode != FleetCommand::VELOCITY) manual_active_ = false;
    command_pub_->publish(c);
    log_event(QString("%1 → %2").arg(label,selected_target()),"#72DDF7");
  }
  void send_simple(std::uint8_t mode,const QString& label) { publish(base_command(mode),label); }

  void begin_manual(double vx,double vy,double vz,double yaw) {
    manual_active_ = true;
    manual_vx_ = vx;
    manual_vy_ = vy;
    manual_vz_ = vz;
    manual_yaw_ = yaw;
    send_velocity(vx,vy,vz,yaw,true);
  }

  void end_manual() {
    if (!manual_active_) return;
    manual_active_ = false;
    send_simple(FleetCommand::HOLD,"HOLD");
  }

  void send_velocity(double vx,double vy,double vz,double yaw,bool record=true) {
    if(std::hypot(vx,vy)>8.0||std::abs(vz)>3.0||std::abs(yaw)>1.2){
      if(record) log_event("Local control envelope rejected velocity command","#FF6685");
      return;
    }
    auto c=base_command(FleetCommand::VELOCITY);
    c.ttl_ms=450;
    c.linear.x=vx;c.linear.y=vy;c.linear.z=vz;c.yaw_rate=yaw;
    command_pub_->publish(c);
    if(record) {
      log_event(QString("VEL [%1, %2, %3] yaw %4 → %5")
        .arg(vx,0,'f',2).arg(vy,0,'f',2).arg(vz,0,'f',2).arg(yaw,0,'f',2)
        .arg(selected_target()),"#72DDF7");
    }
  }
  void send_goto(double x,double y,double z,double speed) {
    if (!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(speed)||
        z<0.0||z>30.0||speed<=0.0||speed>8.0) {
      log_event("Local control envelope rejected position target","#FF6685");
      return;
    }
    auto c=base_command(FleetCommand::GOTO_POSITION);
    c.target_position.x=x;c.target_position.y=y;c.target_position.z=z;c.max_speed_mps=speed;
    publish(c,QString("GOTO [%1, %2, %3] @ %4 m/s")
      .arg(x,0,'f',1).arg(y,0,'f',1).arg(z,0,'f',1).arg(speed,0,'f',1));
  }

  void parse_command() {
    const QString raw=command_line_->text().trimmed(); if(raw.isEmpty())return;
    const auto parts=raw.simplified().split(' '); const QString cmd=parts[0].toLower();
    if(cmd=="hold")send_simple(FleetCommand::HOLD,"HOLD");
    else if(cmd=="land")send_simple(FleetCommand::LAND,"LAND");
    else if(cmd=="rtl"||cmd=="return"||cmd=="home")send_simple(FleetCommand::RETURN_HOME,"RETURN_HOME");
    else if(cmd=="stop"||cmd=="estop")send_simple(FleetCommand::EMERGENCY_STOP,"EMERGENCY_STOP");
    else if(cmd=="takeoff"&&parts.size()==2){bool ok=false;double altitude=parts[1].toDouble(&ok);if(!ok||altitude<0.2||altitude>30.0)log_event("takeoff altitude must be 0.2..30 m","#FFB45B");else{auto c=base_command(FleetCommand::TAKEOFF);c.takeoff_altitude_m=altitude;publish(c,QString("TAKEOFF %1 m").arg(altitude,0,'f',2));}}
    else if(cmd=="goto"&&parts.size()==5){bool a=false,b=false,c=false,d=false;double x=parts[1].toDouble(&a),y=parts[2].toDouble(&b),z=parts[3].toDouble(&c),speed=parts[4].toDouble(&d);if(a&&b&&c&&d)send_goto(x,y,z,speed);else log_event("goto syntax: goto x y z max_speed","#FFB45B");}
    else if(cmd=="vel"&&parts.size()==5){bool a=false,b=false,c=false,d=false;double vx=parts[1].toDouble(&a),vy=parts[2].toDouble(&b),vz=parts[3].toDouble(&c),yaw=parts[4].toDouble(&d);if(a&&b&&c&&d)send_velocity(vx,vy,vz,yaw);else log_event("vel syntax: vel vx vy vz yaw_rate","#FFB45B");}
    else log_event("Unknown command. Use goto, takeoff, vel, land, rtl, hold or stop.","#FFB45B");
    command_line_->clear();
  }
  void ask_ai() {
    const QString q=ai_query_->text().trimmed(); if(q.isEmpty())return; std_msgs::msg::String msg; msg.data=q.toStdString(); ai_query_pub_->publish(msg);
    ai_answer_->setMarkdown("Analyzing the latest bounded fleet snapshot…"); log_event("AI advisor query submitted","#A899FF"); ai_query_->clear();
  }
  void set_item(int row,int col,const QString& text,const QColor& color=QColor()) {
    auto* item=table_->item(row,col); if(!item){item=new QTableWidgetItem;table_->setItem(row,col,item);} item->setText(text);if(color.isValid())item->setForeground(color);
  }
  void on_telemetry(const Telemetry& t) {
    auto it=fleet_.find(t.drone_id); if(it==fleet_.end()){FleetEntry e;e.row=table_->rowCount();table_->insertRow(e.row);e.battery_bar=new QProgressBar;e.link_bar=new QProgressBar;e.battery_bar->setRange(0,100);e.link_bar->setRange(0,100);table_->setCellWidget(e.row,4,e.battery_bar);table_->setCellWidget(e.row,5,e.link_bar);it=fleet_.emplace(t.drone_id,std::move(e)).first;target_->addItem(QString::fromStdString(t.drone_id),QString::fromStdString(t.drone_id));log_event(QString("Discovered %1").arg(QString::fromStdString(t.drone_id)),"#50E3C2");}
    auto& e=it->second;e.telemetry=t;e.last_seen=std::chrono::steady_clock::now();int r=e.row;QColor normal("#DCEBFA"),warn("#FF6685");
    set_item(r,0,QString::fromStdString(t.drone_id));set_item(r,1,mode_name(t.mode),t.failsafe_active?warn:normal);
    set_item(r,2,QString("%1, %2, %3").arg(t.position.x,0,'f',1).arg(t.position.y,0,'f',1).arg(t.position.z,0,'f',1));
    set_item(r,3,QString("%1, %2, %3").arg(t.velocity.x,0,'f',1).arg(t.velocity.y,0,'f',1).arg(t.velocity.z,0,'f',1));
    e.battery_bar->setValue(static_cast<int>(std::round(t.battery_pct)));e.link_bar->setValue(static_cast<int>(std::round(t.link_quality_pct)));
    set_item(r,6,QString::number(t.peer_count));set_item(r,7,t.armed?"ARMED":"SAFE");
    set_item(r,8,t.failsafe_active?QString::fromStdString(t.failsafe_reason):"OK",t.failsafe_active?warn:QColor("#62E8C5"));
    set_item(r,9,QString("%1 ms").arg(t.manager_heartbeat_age_ms));
  }
  void refresh_health() {
    auto now=std::chrono::steady_clock::now();int active=0,failsafes=0;double battery=0,link=0;std::vector<RadarPoint> points;points.reserve(fleet_.size());QString target=selected_target();
    for(const auto& [id,e]:fleet_){auto age=std::chrono::duration_cast<std::chrono::milliseconds>(now-e.last_seen);if(age>std::chrono::milliseconds(2000))continue;++active;battery+=e.telemetry.battery_pct;link+=e.telemetry.link_quality_pct;if(e.telemetry.failsafe_active)++failsafes;points.push_back({QString::fromStdString(id),QPointF(e.telemetry.position.x,e.telemetry.position.y),e.telemetry.battery_pct,e.telemetry.failsafe_active,target=="*"||target==QString::fromStdString(id)});}
    active_card_->set_value(QString::number(active),QString("%1 discovered").arg(fleet_.size()));battery_card_->set_value(active?QString("%1%").arg(battery/active,0,'f',0):"--");link_card_->set_value(active?QString("%1%").arg(link/active,0,'f',0):"--");failsafe_card_->set_value(QString::number(failsafes),failsafes==0?"nominal":"operator attention");connection_->setText(active?"●  ROS BUS LIVE":"●  WAITING");radar_->set_points(std::move(points));
  }
  void log_event(const QString& text,const QString& color) {
    QString time=QDateTime::currentDateTime().toString("HH:mm:ss");event_log_->append(QString("<span style='color:#536D87'>%1</span> &nbsp; <span style='color:%2'>%3</span>").arg(time,color,text.toHtmlEscaped()));
  }

  std::uint64_t sequence_{0};std::shared_ptr<rclcpp::Node> node_;rclcpp::Publisher<FleetCommand>::SharedPtr command_pub_;rclcpp::Publisher<std_msgs::msg::String>::SharedPtr ai_query_pub_;rclcpp::Subscription<Telemetry>::SharedPtr telemetry_sub_;rclcpp::Subscription<std_msgs::msg::String>::SharedPtr ai_answer_sub_;
  QComboBox* target_{};QLabel* connection_{};MetricCard* active_card_{};MetricCard* battery_card_{};MetricCard* link_card_{};MetricCard* failsafe_card_{};FleetRadar* radar_{};QTableWidget* table_{};QDoubleSpinBox* horizontal_speed_{};QDoubleSpinBox* vertical_speed_{};QDoubleSpinBox* yaw_rate_{};QDoubleSpinBox* takeoff_alt_{};QDoubleSpinBox* goto_x_{};QDoubleSpinBox* goto_y_{};QDoubleSpinBox* goto_z_{};QDoubleSpinBox* goto_speed_{};QLineEdit* command_line_{};QLineEdit* ai_query_{};QTextBrowser* ai_answer_{};QTextBrowser* event_log_{};QTimer* ros_timer_{};QTimer* ui_timer_{};QTimer* manual_timer_{};
  bool manual_active_{false};
  double manual_vx_{0.0},manual_vy_{0.0},manual_vz_{0.0},manual_yaw_{0.0};
  std::unordered_map<std::string,FleetEntry> fleet_;
};

int main(int argc,char** argv) {
  rclcpp::init(argc,argv);QApplication app(argc,argv);app.setApplicationName("AERION Fleet Operations");app.setOrganizationName("drone-system");app.setStyle("Fusion");
  QFile theme(":/assets/theme.qss");if(theme.open(QIODevice::ReadOnly|QIODevice::Text))app.setStyleSheet(QString::fromUtf8(theme.readAll()));
  FleetConsole window;window.show();int code=app.exec();rclcpp::shutdown();return code;
}
