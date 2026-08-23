"""Execute physical probes and compare the recorded distribution budgets."""

from __future__ import annotations

import sys
from pathlib import Path


def run(context, devices) -> None:
    evidence = Path(getattr(context, "performance_evidence", "artifacts/release-evidence/performance.json"))
    ports = context.discover_ports(devices)
    for role, port in ports.items():
        context.run(f"performance probe {role}", [sys.executable, "scripts/testops.py", "-p", port, "await_flipper"], timeout=context.timeout + 30)
    context.run("compare physical performance budgets", [sys.executable, "tools/release/compare_budgets.py", str(evidence)], timeout=context.timeout)
    context.observations["performanceEvidence"] = str(evidence)
