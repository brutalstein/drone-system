#include <gtest/gtest.h>
#include "drone_system_core/state_machine.hpp"

using drone_system_core::Command;
using drone_system_core::Mode;
using drone_system_core::StateMachine;

TEST(StateMachine, RejectsDuplicateSequence) {
  StateMachine sm;
  const auto now = StateMachine::Clock::now();
  Command c;
  c.sequence = 7;
  c.mode = Mode::Hold;
  EXPECT_TRUE(sm.accept(c, now, now));
  EXPECT_FALSE(sm.accept(c, now, now));
}

TEST(StateMachine, RejectsOverspeed) {
  StateMachine sm;
  const auto now = StateMachine::Clock::now();
  Command c;
  c.sequence = 1;
  c.mode = Mode::Velocity;
  c.vx = 100.0;
  EXPECT_FALSE(sm.accept(c, now, now));
}

TEST(StateMachine, LinkLossTransitionsHoldThenLand) {
  StateMachine sm;
  const auto t0 = StateMachine::Clock::now();
  Command c;
  c.sequence = 1;
  c.mode = Mode::Velocity;
  c.vx = 1.0;
  ASSERT_TRUE(sm.accept(c, t0, t0));

  sm.tick(t0 + std::chrono::milliseconds(1300), t0, 100.0);
  EXPECT_EQ(sm.setpoint().mode, Mode::Hold);
  EXPECT_TRUE(sm.failsafe_active());

  sm.tick(t0 + std::chrono::milliseconds(5100), t0, 100.0);
  EXPECT_EQ(sm.setpoint().mode, Mode::Land);
}

TEST(StateMachine, LowBatteryForcesLand) {
  StateMachine sm;
  const auto now = StateMachine::Clock::now();
  sm.tick(now, now, 5.0);
  EXPECT_EQ(sm.setpoint().mode, Mode::Land);
  EXPECT_TRUE(sm.failsafe_active());
}

TEST(StateMachine, ExternalPeerFailsafeForcesHoldAndNeedsFreshCommandToClear) {
  StateMachine sm;
  const auto now = StateMachine::Clock::now();
  Command c;
  c.sequence = 1;
  c.mode = Mode::Velocity;
  c.vx = 1.0;
  ASSERT_TRUE(sm.accept(c, now, now));

  sm.external_hold("peer heartbeat degraded");
  EXPECT_EQ(sm.setpoint().mode, Mode::Hold);
  EXPECT_TRUE(sm.failsafe_active());
  EXPECT_EQ(sm.failsafe_reason(), "peer heartbeat degraded");

  c.sequence = 2;
  c.mode = Mode::Hold;
  EXPECT_TRUE(sm.accept(c, now, now));
  EXPECT_FALSE(sm.failsafe_active());
}
