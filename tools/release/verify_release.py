#!/usr/bin/env python3
"""Verify a release manifest and every referenced component digest."""

from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import json
from pathlib import Path
import re

DIGEST = re.compile(r"^[0-9a-f]{64}$")
VERSION = re.compile(r"^\d+\.\d+\.\d+$")
KEY_ID = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")


def verify_manifest(manifest_path: Path, root: Path) -> list[str]:
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return [f"cannot read manifest: {error}"]
    problems: list[str] = []
    if not isinstance(manifest, dict) or manifest.get("schema") != "poison.release-manifest/v1": return ["invalid release manifest schema"]
    for key in ("version", "rollbackVersion"):
        if not isinstance(manifest.get(key), str) or not VERSION.fullmatch(manifest[key]): problems.append(f"invalid {key}")
    for key in ("minimumVersion", "maximumVersion"):
        if key in manifest and (not isinstance(manifest[key], str) or not VERSION.fullmatch(manifest[key])): problems.append(f"invalid {key}")
    if manifest.get("channel") not in {"internal", "developer", "beta", "stable"}: problems.append("invalid channel")
    if not isinstance(manifest.get("target"), str) or not re.fullmatch(r"[a-z0-9._-]{1,32}", manifest["target"]): problems.append("invalid target")
    components = manifest.get("components")
    if not isinstance(components, list) or not components: return [*problems, "components must be non-empty"]
    seen: set[str] = set()
    for component in components:
        if not isinstance(component, dict): problems.append("component is not an object"); continue
        identifier = component.get("id")
        if not isinstance(identifier, str) or identifier in seen: problems.append(f"duplicate or invalid component: {identifier}"); continue
        seen.add(identifier)
        path_value = component.get("path")
        if not isinstance(path_value, str) or Path(path_value).is_absolute() or ".." in Path(path_value).parts: problems.append(f"unsafe component path: {identifier}"); continue
        expected = component.get("sha256")
        size = component.get("bytes")
        if not isinstance(expected, str) or not DIGEST.fullmatch(expected) or not isinstance(size, int) or size < 1: problems.append(f"invalid component metadata: {identifier}"); continue
        path = (root / path_value).resolve()
        try: path.relative_to(root.resolve())
        except ValueError: problems.append(f"component escapes root: {identifier}"); continue
        if not path.is_file(): problems.append(f"missing component: {identifier}"); continue
        data = path.read_bytes()
        if len(data) != size or hashlib.sha256(data).hexdigest() != expected: problems.append(f"component digest mismatch: {identifier}")
    revocations = manifest.get("revocations")
    if not isinstance(revocations, list) or len(revocations) != len(set(revocations)) or any(not isinstance(item, str) or not DIGEST.fullmatch(item) for item in revocations):
        problems.append("invalid revocations")
    signature = manifest.get("signature")
    if signature is not None:
        if not isinstance(signature, dict) or set(signature) != {"algorithm", "keyId", "value"} or signature.get("algorithm") != "ECDSA-P256-SHA256" or not isinstance(signature.get("keyId"), str) or not KEY_ID.fullmatch(signature["keyId"]) or not isinstance(signature.get("value"), str):
            problems.append("invalid signature metadata")
        else:
            try: base64.b64decode(signature["value"], validate=True)
            except (ValueError, binascii.Error): problems.append("invalid signature encoding")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    args = parser.parse_args()
    problems = verify_manifest(args.manifest, args.root)
    print(json.dumps({"schema": "poison.release-verification/v1", "result": "PASS" if not problems else "FAIL", "problems": problems}, indent=2, sort_keys=True))
    return 0 if not problems else 1


if __name__ == "__main__": raise SystemExit(main())
