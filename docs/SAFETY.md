# Safety model

1. Validate target ID, TTL, sequence, finite numeric fields and configured limits.
2. Cap fleet registry and queue depths.
3. Use a monotonic watchdog: link loss -> HOLD -> LAND.
4. Never auto-resume an old mission after a failsafe.
5. Force LAND below configured simulated battery threshold.
6. Keep Grok outside the control path.
7. Provide explicit link-loss fault injection for testing.
8. Reject unknown modes and malformed numeric values.

No software can honestly be guaranteed to "never fail". This project therefore makes failure modes explicit, testable and observable. It is not aviation certification.
