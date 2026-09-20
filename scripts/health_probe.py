#!/usr/bin/env python3
import argparse
import json
import os
import time
from pathlib import Path

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy

from drone_system_interfaces.msg import DroneTelemetry, Heartbeat


class Probe(Node):
    def __init__(self):
        super().__init__("drone_system_health_probe")
        self.telemetry = {}
        self.manager_heartbeat_count = 0

        telemetry_qos = QoSProfile(
            history=HistoryPolicy.KEEP_LAST,
            depth=8,
            reliability=ReliabilityPolicy.BEST_EFFORT,
        )
        reliable_qos = QoSProfile(
            history=HistoryPolicy.KEEP_LAST,
            depth=16,
            reliability=ReliabilityPolicy.RELIABLE,
        )

        self.create_subscription(
            DroneTelemetry, "/fleet/telemetry", self.on_telemetry, telemetry_qos
        )
        self.create_subscription(
            Heartbeat, "/fleet/manager_heartbeat", self.on_manager_heartbeat, reliable_qos
        )

    def on_telemetry(self, msg):
        self.telemetry[msg.drone_id] = {
            "sequence": int(msg.sequence),
            "battery_pct": float(msg.battery_pct),
            "link_quality_pct": float(msg.link_quality_pct),
            "peer_count": int(msg.peer_count),
            "mode": int(msg.mode),
            "armed": bool(msg.armed),
            "failsafe_active": bool(msg.failsafe_active),
            "failsafe_reason": msg.failsafe_reason,
            "manager_heartbeat_age_ms": int(msg.manager_heartbeat_age_ms),
            "last_seen_monotonic": time.monotonic(),
        }

    def on_manager_heartbeat(self, _msg):
        self.manager_heartbeat_count += 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--drones", type=int, required=True)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    expected = {f"drone_{i}" for i in range(1, args.drones + 1)}
    rclpy.init()
    node = Probe()
    deadline = time.monotonic() + args.timeout

    try:
        while time.monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.15)
            present = expected.intersection(node.telemetry.keys())
            mesh_ok = all(
                node.telemetry[d]["peer_count"] >= max(0, args.drones - 1)
                for d in present
            )
            if present == expected and node.manager_heartbeat_count >= 2 and mesh_ok:
                break

        now = time.monotonic()
        present = expected.intersection(node.telemetry.keys())
        missing = sorted(expected - present)
        stale = sorted(
            d for d in present
            if now - node.telemetry[d]["last_seen_monotonic"] > 2.0
        )
        degraded = sorted(
            d for d in present
            if node.telemetry[d]["link_quality_pct"] <= 0.0
            or node.telemetry[d]["manager_heartbeat_age_ms"] >= 1200
        )
        mesh_degraded = sorted(
            d for d in present
            if node.telemetry[d]["peer_count"] < max(0, args.drones - 1)
        )

        healthy = (
            not missing
            and not stale
            and not degraded
            and not mesh_degraded
            and node.manager_heartbeat_count >= 2
        )

        report = {
            "generated_at_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "healthy": healthy,
            "expected_drone_count": args.drones,
            "seen_drone_count": len(present),
            "manager_heartbeat_count": node.manager_heartbeat_count,
            "missing_drones": missing,
            "stale_drones": stale,
            "degraded_link_drones": degraded,
            "mesh_degraded_drones": mesh_degraded,
            "telemetry": {
                key: {k: v for k, v in value.items() if k != "last_seen_monotonic"}
                for key, value in sorted(node.telemetry.items())
            },
        }

        output = Path(args.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps(report, indent=2))
        return 0 if healthy else 20
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
