"""Run native Rust admission and loader checks on physical firmware."""

from __future__ import annotations

import sys


def run(context, devices) -> None:
    context.run("verify Rust SDK and native admission", [sys.executable, "tools/rust/verify_vendor.py", "--locked"], timeout=120)
    context.run("build native Rust firmware image", ["./fbt", "FIRMWARE_APP_SET=unit_tests", "firmware_all"], timeout=1800)
    ports = context.discover_ports(devices)
    for role, device in devices.items():
        context.run(f"run native Rust admission units on {role}", [sys.executable, "scripts/testops.py", "-p", ports[role], "run_units", "--output", str(context.results_dir / f"{role}-rust-native.txt")], timeout=1800)
