#!/usr/bin/env python3
"""Verify diagnostic/support artifacts contain no prohibited sensitive material."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
from typing import Any

FORBIDDEN_FIELDS = re.compile(r"(?:secret|private[_-]?key|access[_-]?token|authorization|credential|raw[_-]?payload)", re.IGNORECASE)
FORBIDDEN_TEXT = re.compile(r"(?:-----BEGIN[^-]+PRIVATE KEY-----|bearer\s+[A-Za-z0-9._~-]{12,}|api[_-]?key\s*[:=])", re.IGNORECASE)


def _walk(value: Any, location: str, failures: list[str]) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            if isinstance(key, str) and FORBIDDEN_FIELDS.search(key): failures.append(f"{location}.{key}: prohibited field")
            _walk(child, f"{location}.{key}", failures)
    elif isinstance(value, list):
        for index, child in enumerate(value): _walk(child, f"{location}[{index}]", failures)
    elif isinstance(value, str) and FORBIDDEN_TEXT.search(value):
        failures.append(f"{location}: prohibited secret-like text")


def scan(path: Path) -> list[str]:
    try:
        raw = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        return [f"{path}: cannot read: {error}"]
    failures: list[str] = []
    if path.suffix.lower() == ".json":
        try:
            _walk(json.loads(raw), "$", failures)
        except json.JSONDecodeError as error:
            failures.append(f"{path}: invalid JSON: {error}")
    elif FORBIDDEN_TEXT.search(raw):
        failures.append(f"{path}: prohibited secret-like text")
    return [f"{path}: {failure}" if not failure.startswith(str(path)) else failure for failure in failures]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path, nargs="+")
    args = parser.parse_args()
    failures = [failure for path in args.path for failure in scan(path)]
    print(json.dumps({"schema": "poison.redaction-verification/v1", "result": "PASS" if not failures else "FAIL", "failures": failures}, indent=2, sort_keys=True))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
