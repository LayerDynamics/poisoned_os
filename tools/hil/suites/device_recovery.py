"""Exercise dashboard-independent recovery controls on a real device."""

from __future__ import annotations

import sys


def run(context, devices) -> None:
    recovery = devices.get("recovery")
    if recovery is None or recovery.recovery is None:
        raise RuntimeError("device-recovery requires an inventory recovery device")
    ports = context.discover_ports(devices)
    context.run("build recovery-enabled firmware", ["./fbt", "firmware_all", "updater_all", "resources"], timeout=1800)
    context.run(
        "verify recovery device CLI is available",
        [sys.executable, "scripts/testops.py", "-p", ports["recovery"], "await_flipper"],
        timeout=context.timeout + 30,
    )
    context.run(
        "capture recovery device baseline",
        [sys.executable, "scripts/testops.py", "-p", ports["recovery"], "probe_baseline", "--role", "recovery", "--sd-sentinel", recovery.sd_fixture.sentinel_path, "--sd-sha256", recovery.sd_fixture.sha256, "--evidence", str(context.results_dir / "recovery-baseline.json")],
        timeout=context.timeout + 30,
    )
