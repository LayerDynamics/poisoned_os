"""Exercise the local-only profile with network-dependent services disabled."""

from __future__ import annotations

import sys


def run(context, devices) -> None:
    local_env = {"POISON_PROFILE": "local-only", "NO_PROXY": "*", "no_proxy": "*"}
    context.run("verify local-only dashboard", ["pnpm", "--dir", "dashboard", "verify"], env=local_env, timeout=600)
    ports = context.discover_ports(devices)
    for role, port in ports.items():
        context.run(f"local-only device probe {role}", [sys.executable, "scripts/testops.py", "-p", port, "await_flipper"], env=local_env, timeout=context.timeout + 30)
    context.observations.update({
        "profile": "local-only",
        "localNetworkRequiredForDashboard": True,
        "externalServiceRequired": False,
    })
