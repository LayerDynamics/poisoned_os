"""Run the JavaScript limiter and recovery workflow on a real Flipper."""

from __future__ import annotations

import sys

from suites.firmware_units import _await_device, _flash


def run(context, devices) -> None:
    ports = context.discover_ports(devices)
    context.run(
        "build JavaScript limiter firmware",
        ["./fbt", "FIRMWARE_APP_SET=unit_tests", "firmware_all"],
        timeout=1800,
    )
    for role, device in devices.items():
        port = _flash(context, device, ports[role], "unit_tests")
        context.run(
            f"run JavaScript limiter units on {role}",
            [sys.executable, "scripts/testops.py", "-p", port, "run_units", "--output", str(context.results_dir / f"{role}-javascript-limits.txt")],
            timeout=1800,
        )
        context.run(
            f"verify {role} remains responsive after JavaScript limits",
            [sys.executable, "scripts/testops.py", "-p", _await_device(context, device), "await_flipper"],
            timeout=context.timeout + 30,
        )
