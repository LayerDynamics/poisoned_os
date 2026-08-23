#!/usr/bin/env python3
"""Verify that required release runbooks contain actionable operator sections."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

REQUIRED = (
    "release.md", "rollback.md", "key-compromise.md", "builder-isolation.md",
    "support-triage.md", "storage-corruption-recovery.md", "device-recovery.md", "evidence-recovery.md",
)
SECTIONS = ("Owner", "Prerequisites", "Procedure", "Verification", "Escalation")


def verify(root: Path) -> list[str]:
    failures: list[str] = []
    for filename in REQUIRED:
        path = root / filename
        if not path.is_file():
            failures.append(f"missing runbook: {filename}")
            continue
        text = path.read_text(encoding="utf-8")
        if not re.search(r"^#\s+", text, re.MULTILINE): failures.append(f"{filename}: missing title")
        for section in SECTIONS:
            if not re.search(rf"^##\s+{re.escape(section)}\s*$", text, re.MULTILINE): failures.append(f"{filename}: missing section {section}")
        if not re.search(r"```(?:bash|sh)\n", text): failures.append(f"{filename}: missing executable command block")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("docs/runbooks"))
    args = parser.parse_args()
    failures = verify(args.root)
    print(json.dumps({"schema": "poison.runbook-verification/v1", "result": "PASS" if not failures else "FAIL", "failures": failures}, indent=2, sort_keys=True))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
