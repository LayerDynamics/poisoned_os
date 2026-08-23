#!/usr/bin/env python3
"""Verify that every normative specification requirement has current evidence."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

SCHEMA = "poison.release.requirement-evidence/v1"
ID_PATTERN = re.compile(r"\b(?:FR|NFR|REL)-[0-9A-Z._-]+\b")
VERSION_PATTERN = re.compile(r"^v?\d+(?:\.\d+){0,3}(?:[-+][A-Za-z0-9.-]+)?$")


class EvidenceError(ValueError):
    pass


def requirement_ids(spec: Path) -> set[str]:
    return set(ID_PATTERN.findall(spec.read_text(encoding="utf-8")))


def _load(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise EvidenceError(f"cannot load evidence ledger {path}: {error}") from error
    if not isinstance(value, dict) or value.get("schema") != SCHEMA or not isinstance(value.get("requirements"), list):
        raise EvidenceError(f"evidence ledger must use {SCHEMA}")
    return value


def _validate_entry(entry: object, index: int, evidence_root: Path) -> tuple[str, str | None]:
    if not isinstance(entry, dict):
        return f"entry {index} is not an object", None
    required = {"id", "milestone", "command", "evidence", "version", "result", "reviewer", "timestamp"}
    missing = required - entry.keys()
    if missing:
        return f"entry {index} missing {', '.join(sorted(missing))}", None
    identifier = entry.get("id")
    if not isinstance(identifier, str) or not ID_PATTERN.fullmatch(identifier):
        return f"entry {index} has invalid id", None
    if not isinstance(entry["milestone"], str) or not re.fullmatch(r"M[0-6]", entry["milestone"]):
        return f"{identifier} has invalid milestone", identifier
    if not all(isinstance(entry[field], str) and entry[field].strip() for field in ("command", "evidence", "version", "reviewer", "timestamp")):
        return f"{identifier} has an empty required field", identifier
    if entry["result"] not in {"PASS", "FAIL", "BLOCKED"}:
        return f"{identifier} has invalid result", identifier
    if not VERSION_PATTERN.fullmatch(entry["version"]):
        return f"{identifier} has invalid version", identifier
    evidence = Path(entry["evidence"])
    if evidence.is_absolute() or ".." in evidence.parts:
        return f"{identifier} evidence path escapes root", identifier
    resolved = (evidence_root / evidence).resolve()
    try:
        resolved.relative_to(evidence_root.resolve())
    except ValueError:
        return f"{identifier} evidence path escapes root", identifier
    if not resolved.is_file():
        return f"{identifier} evidence file is missing: {entry['evidence']}", identifier
    physical = entry.get("physicalIds", [])
    if not isinstance(physical, list) or any(not isinstance(item, str) or not item.strip() or item.upper().startswith(("EXAMPLE", "REPLACE", "CHANGE_ME")) for item in physical):
        return f"{identifier} has synthetic or invalid physical IDs", identifier
    return "", identifier


def verify(spec: Path, evidence_root: Path) -> list[str]:
    expected = requirement_ids(spec)
    ledger_path = evidence_root / "v1-requirements.json"
    if not ledger_path.is_file():
        return [f"missing ledger: {ledger_path}", *[f"missing evidence for {item}" for item in sorted(expected)]]
    ledger = _load(ledger_path)
    problems: list[str] = []
    seen: set[str] = set()
    entries = ledger["requirements"]
    for index, entry in enumerate(entries):
        problem, identifier = _validate_entry(entry, index, evidence_root)
        if problem:
            problems.append(problem)
        if identifier:
            if identifier in seen:
                problems.append(f"duplicate evidence entry: {identifier}")
            seen.add(identifier)
            if entry.get("result") != "PASS":
                problems.append(f"{identifier} result is {entry.get('result')}, not PASS")
    for identifier in sorted(expected - seen):
        problems.append(f"missing evidence for {identifier}")
    for identifier in sorted(seen - expected):
        problems.append(f"evidence has unknown requirement: {identifier}")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        problems = verify(args.spec, args.evidence)
    except EvidenceError as error:
        problems = [str(error)]
    if problems:
        print(json.dumps({"schema": "poison.release.verification/v1", "result": "FAIL", "problems": problems}, indent=2, sort_keys=True))
        return 1
    print(json.dumps({"schema": "poison.release.verification/v1", "result": "PASS", "problems": []}, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
