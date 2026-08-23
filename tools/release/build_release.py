#!/usr/bin/env python3
"""Assemble a deterministic release archive and its digest manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile

SCHEMA = "poison.release-manifest/v1"
CHANNELS = {"internal", "developer", "beta", "stable"}


def canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False) + "\n").encode()


def _version(value: str) -> bool:
    parts = value.split(".")
    return len(parts) == 3 and all(part.isdigit() for part in parts)


def build(input_root: Path, output: Path, version: str, channel: str, target: str, rollback_version: str) -> dict:
    if not _version(version) or not _version(rollback_version): raise ValueError("versions must be numeric semver")
    if channel not in CHANNELS: raise ValueError("invalid channel")
    files = sorted(path for path in input_root.rglob("*") if path.is_file() and path.name != "release.json")
    if not files: raise ValueError("release input is empty")
    components = []
    for path in files:
        relative = path.relative_to(input_root).as_posix()
        if not relative or ".." in PurePosixPath(relative).parts: raise ValueError("unsafe component path")
        data = path.read_bytes()
        components.append({"id": relative.replace("/", ".")[:64], "path": relative, "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)})
    manifest = {"schema": SCHEMA, "version": version, "channel": channel, "target": target, "rollbackVersion": rollback_version, "components": components, "revocations": []}
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_STORED) as archive:
        info = zipfile.ZipInfo("release.json", date_time=(1980, 1, 1, 0, 0, 0)); info.external_attr = 0o100644 << 16; archive.writestr(info, canonical(manifest))
        for component in components:
            info = zipfile.ZipInfo(component["path"], date_time=(1980, 1, 1, 0, 0, 0)); info.external_attr = 0o100644 << 16; archive.writestr(info, (input_root / component["path"]).read_bytes())
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(); parser.add_argument("--input-root", type=Path, required=True); parser.add_argument("--output", type=Path, required=True); parser.add_argument("--version", required=True); parser.add_argument("--channel", required=True); parser.add_argument("--target", required=True); parser.add_argument("--rollback-version", required=True)
    args = parser.parse_args()
    try: build(args.input_root, args.output, args.version, args.channel, args.target, args.rollback_version); print(hashlib.sha256(args.output.read_bytes()).hexdigest()); return 0
    except (OSError, ValueError) as error: parser.error(str(error)); return 2


if __name__ == "__main__": raise SystemExit(main())
