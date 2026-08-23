#!/usr/bin/env python3
"""Inventory and stage byte-preserving OFW/Momentum-style legacy files."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
from typing import Any

try:
    from tools.migration.verify_manifest import verify_manifest
except ModuleNotFoundError:
    from verify_manifest import verify_manifest

CONVERTER_VERSION = "1.0.0"
MAX_ENTRIES = 4096


def _digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _logical_path(relative: PurePosixPath) -> tuple[str, str]:
    payload = PurePosixPath(*relative.parts[1:]) if relative.parts and relative.parts[0] in {"scripts", "captures", "settings", "apps"} and len(relative.parts) > 1 else relative
    suffix = relative.suffix.lower()
    if suffix in {".sub", ".rfid", ".ibtn", ".nfc", ".ir"}:
        return f"/captures/{payload.as_posix()}", "compatible"
    if suffix in {".txt", ".json", ".csv", ".js", ".py"}:
        return f"/scripts/{payload.as_posix()}", "convertible"
    if suffix in {".cfg", ".ini", ".yaml", ".yml"}:
        return f"/settings/{payload.as_posix()}", "convertible"
    return f"/unknown/{payload.as_posix()}", "unknown"


def inventory_legacy(source_root: Path, backup_root: Path, converter_version: str = CONVERTER_VERSION) -> dict[str, Any]:
    """Create a deterministic manifest and verified backup for int/ext files."""
    entries: list[dict[str, Any]] = []
    for namespace in ("int", "ext"):
        namespace_root = source_root / namespace
        if not namespace_root.is_dir():
            continue
        for path in sorted((item for item in namespace_root.rglob("*") if item.is_file()), key=lambda item: item.relative_to(source_root).as_posix()):
            relative = PurePosixPath(path.relative_to(namespace_root).as_posix())
            if not relative.parts or ".." in relative.parts or any(not part for part in relative.parts):
                continue
            data = path.read_bytes()
            backup_relative = PurePosixPath("legacy") / namespace / relative
            backup_path = backup_root / Path(backup_relative.as_posix())
            backup_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, backup_path)
            logical_path, classification = _logical_path(relative)
            entries.append({
                "sourcePath": f"/{namespace}/{relative.as_posix()}",
                "sourceSha256": _digest(data),
                "bytes": len(data),
                "logicalPath": logical_path,
                "classification": classification,
                "requiredFreeBytes": len(data),
                "backup": {"path": backup_relative.as_posix(), "sha256": _digest(backup_path.read_bytes()), "bytes": backup_path.stat().st_size, "verified": True},
            })
    if not entries or len(entries) > MAX_ENTRIES:
        raise ValueError("legacy inventory must contain 1..4096 files")
    return {"schema": "poison.migration-manifest/v1", "converterVersion": converter_version, "entries": entries}


def stage_manifest(manifest: dict[str, Any], source_root: Path, backup_root: Path, target_root: Path) -> list[str]:
    """Stage compatible/convertible entries without mutating source or backups."""
    manifest_path = target_root / ".migration-manifest.json"
    target_root.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, sort_keys=True, separators=(",", ":")), encoding="utf-8")
    temp_manifest = target_root / ".migration-verify-input.json"
    temp_manifest.write_text(json.dumps(manifest), encoding="utf-8")
    problems = verify_manifest(temp_manifest, source_root, backup_root)
    temp_manifest.unlink()
    if problems:
        raise ValueError("; ".join(problems))
    staged: list[str] = []
    for entry in manifest["entries"]:
        if entry["classification"] not in {"compatible", "convertible"}:
            continue
        source = source_root / entry["sourcePath"].lstrip("/")
        destination = target_root / entry["logicalPath"].lstrip("/")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        if _digest(destination.read_bytes()) != entry["sourceSha256"]:
            raise ValueError(f"staged digest mismatch: {entry['sourcePath']}")
        staged.append(entry["logicalPath"])
    return staged


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    inventory = subparsers.add_parser("inventory")
    inventory.add_argument("source_root", type=Path)
    inventory.add_argument("backup_root", type=Path)
    inventory.add_argument("manifest", type=Path)
    stage = subparsers.add_parser("stage")
    stage.add_argument("manifest", type=Path)
    stage.add_argument("source_root", type=Path)
    stage.add_argument("backup_root", type=Path)
    stage.add_argument("target_root", type=Path)
    args = parser.parse_args()
    if args.command == "inventory":
        document = inventory_legacy(args.source_root, args.backup_root)
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        return 0
    document = json.loads(args.manifest.read_text(encoding="utf-8"))
    print(json.dumps({"staged": stage_manifest(document, args.source_root, args.backup_root, args.target_root)}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
