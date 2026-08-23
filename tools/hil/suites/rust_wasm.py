"""Run the Wasm admission/limit regression workflow on physical firmware."""

from __future__ import annotations

import sys


def run(context, devices) -> None:
    context.run("build Rust Wasm boundary firmware", ["./fbt", "FIRMWARE_APP_SET=unit_tests", "firmware_all"], timeout=1800)
    ports = context.discover_ports(devices)
    for role, device in devices.items():
        context.run(f"run Wasm boundary units on {role}", [sys.executable, "scripts/testops.py", "-p", ports[role], "run_units", "--output", str(context.results_dir / f"{role}-rust-wasm.txt")], timeout=1800)
