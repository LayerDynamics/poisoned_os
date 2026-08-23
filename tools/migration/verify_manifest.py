#!/usr/bin/env python3
"""Verify a byte-preserving legacy migration manifest and its backup."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
from typing import Any

DIGEST = re.compile(r"^[0-9a-f]{64}$")
VERSION = re.compile(r"^\d+\.\d+\.\d+$")
SOURCE_PREFIXES = ("/int/", "/ext/")
LOGICAL_PREFIXES = ("/apps/", "/scripts/", "/captures/", "/settings/", "/unknown/")


def _safe_relative(value: str) -> bool:
    path = PurePosixPath(value)
    return not path.is_absolute() and ".." not in path.parts and "" not in path.parts


def _check_file(root: Path, relative: str, expected_size: Any, expected_digest: Any, label: str, problems: list[str]) -> None:
    if not isinstance(relative, str) or not _safe_relative(relative):
        problems.append(f"{label} path is unsafe")
        return
    if not isinstance(expected_size, int) or isinstance(expected_size, bool) or expected_size < 0 or not isinstance(expected_digest, str) or not DIGEST.fullmatch(expected_digest):
        problems.append(f"{label} metadata is invalid")
        return
    path = (root / relative).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError:
        problems.append(f"{label} escapes root")
        return
    if not path.is_file():
        problems.append(f"{label} is missing")
        return
    data = path.read_bytes()
    if len(data) != expected_size or hashlib.sha256(data).hexdigest() != expected_digest:
        problems.append(f"{label} digest or size mismatch")


def verify_manifest(manifest_path: Path, source_root: Path, backup_root: Path) -> list[str]:
    try:
        document = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        return [f"cannot read manifest: {error}"]
    problems: list[str] = []
    if not isinstance(document, dict) or document.get("schema") != "poison.migration-manifest/v1":
        return ["invalid migration manifest schema"]
    if not isinstance(document.get("converterVersion"), str) or not VERSION.fullmatch(document["converterVersion"]):
        problems.append("invalid converterVersion")
    entries = document.get("entries")
    if not isinstance(entries, list) or not entries:
        return [*problems, "entries must be non-empty"]
    seen: set[str] = set()
    for index, entry in enumerate(entries):
        label = f"entries[{index}]"
        if not isinstance(entry, dict):
            problems.append(f"{label} must be an object")
            continue
        source = entry.get("sourcePath")
        logical = entry.get("logicalPath")
        if not isinstance(source, str) or not source.startswith(SOURCE_PREFIXES) or source in seen:
            problems.append(f"{label} sourcePath is invalid or duplicated")
        else:
            seen.add(source)
        if not isinstance(logical, str) or not logical.startswith(LOGICAL_PREFIXES):
            problems.append(f"{label} logicalPath is invalid")
        if entry.get("classification") not in {"compatible", "convertible", "unsupported", "unknown"}:
            problems.append(f"{label} classification is invalid")
        if not isinstance(entry.get("requiredFreeBytes"), int) or isinstance(entry.get("requiredFreeBytes"), bool) or entry["requiredFreeBytes"] < 0:
            problems.append(f"{label} requiredFreeBytes is invalid")
        if isinstance(source, str) and source.startswith("/"):
            _check_file(source_root, source[1:], entry.get("bytes"), entry.get("sourceSha256"), f"{label} source", problems)
        backup = entry.get("backup")
        if not isinstance(backup, dict) or backup.get("verified") is not True:
            problems.append(f"{label} backup is not verified")
        elif isinstance(backup, dict):
            _check_file(backup_root, backup.get("path"), backup.get("bytes"), backup.get("sha256"), f"{label} backup", problems)
    return problems


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--backup-root", type=Path, required=True)
    args = parser.parse_args()
    problems = verify_manifest(args.manifest, args.source_root, args.backup_root)
    print(json.dumps({"schema": "poison.migration-verification/v1", "result": "PASS" if not problems else "FAIL", "problems": problems}, indent=2, sort_keys=True))
    return 0 if not problems else 1


if __name__ == "__main__":
    raise SystemExit(main())
