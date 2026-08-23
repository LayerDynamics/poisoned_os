#!/usr/bin/env python3
"""Reject unsupported external-validation claims in public-facing artifacts."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

DISALLOWED = (
    re.compile(r"\bforensic certification\b", re.IGNORECASE),
    re.compile(r"\badmissible evidence\b", re.IGNORECASE),
    re.compile(r"\bcourt[- ]ready\b", re.IGNORECASE),
)


def scan(paths: list[Path], adr: Path) -> list[str]:
    failures: list[str] = []
    if not adr.is_file():
        failures.append(f"missing external-validation decision: {adr}")
    else:
        decision = adr.read_text(encoding="utf-8")
        if not re.search(r"(?i)no external|not commissioned|not claim", decision): failures.append("ADR-0010 does not state the external-validation decision")
    for root in paths:
        candidates = [root] if root.is_file() else sorted(root.rglob("*")) if root.is_dir() else []
        for path in candidates:
            if not path.is_file() or path.suffix.lower() not in {".md", ".mdx", ".tsx", ".json", ".html"}: continue
            text = path.read_text(encoding="utf-8")
            for pattern in DISALLOWED:
                if pattern.search(text): failures.append(f"{path}: unsupported claim {pattern.pattern}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--path", action="append", type=Path, required=True)
    parser.add_argument("--adr", type=Path, default=Path("docs/decisions/ADR-0010-external-validation.md"))
    args = parser.parse_args()
    failures = scan(args.path, args.adr)
    print(json.dumps({"schema": "poison.claim-verification/v1", "result": "PASS" if not failures else "FAIL", "failures": failures}, indent=2, sort_keys=True))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
