#!/usr/bin/env python3
"""Verify the locked, vendored Rust dependency boundary without network access."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import tomllib
from pathlib import Path
from typing import Any


SCHEMA = "poison.rust-dependencies/v1"
SHA256 = re.compile(r"^[0-9a-f]{64}$")
ALLOWED_LICENSES = {"MIT", "Apache-2.0", "BSD-2-Clause", "BSD-3-Clause", "ISC", "Zlib"}


class VendorError(ValueError):
    """Raised when the Rust dependency boundary is invalid."""


def _digest(path: Path) -> str:
    hasher = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                hasher.update(chunk)
    except OSError as error:
        raise VendorError(f"cannot read {path}: {error}") from error
    return hasher.hexdigest()


def _load(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise VendorError(f"cannot load {path}: {error}") from error
    if not isinstance(value, dict):
        raise VendorError("metadata must be an object")
    return value


def _load_lock(path: Path) -> dict[str, Any]:
    try:
        with path.open("rb") as stream:
            value = tomllib.load(stream)
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise VendorError(f"cannot load {path}: {error}") from error
    if not isinstance(value, dict):
        raise VendorError("Cargo.lock must be a TOML object")
    return value


def _package_source(package: dict[str, Any]) -> str:
    source = package.get("source")
    if not isinstance(source, str) or not source:
        raise VendorError(f"{package.get('name', '<unknown>')} has no source")
    if source.startswith("git+") or source.startswith("path+") or source.startswith("http"):
        raise VendorError(f"unapproved source for {package.get('name', '<unknown>')}")
    if not (source.startswith("vendor/") or source.startswith("rust-sdk/crates/")):
        raise VendorError(f"source is not a checked-in local source for {package.get('name', '<unknown>')}")
    return source


def _validate_approved(manifest: dict[str, Any]) -> dict[str, dict[str, Any]]:
    if manifest.get("schema") != SCHEMA:
        raise VendorError(f"schema must equal {SCHEMA}")
    policy = manifest.get("sourcePolicy")
    if not isinstance(policy, dict) or policy.get("offline") is not True:
        raise VendorError("source policy must require offline mode")
    crates = manifest.get("crates")
    if not isinstance(crates, list):
        raise VendorError("crates must be an array")
    indexed: dict[str, dict[str, Any]] = {}
    for crate in crates:
        if not isinstance(crate, dict) or not isinstance(crate.get("name"), str):
            raise VendorError("crate records must have names")
        name = crate["name"]
        if name in indexed:
            raise VendorError(f"duplicate approved crate: {name}")
        if not isinstance(crate.get("version"), str) or not crate["version"]:
            raise VendorError(f"missing version for {name}")
        if not isinstance(crate.get("checksum"), str) or not SHA256.fullmatch(crate["checksum"]):
            raise VendorError(f"invalid checksum for {name}")
        if crate.get("license") not in ALLOWED_LICENSES:
            raise VendorError(f"license denied for {name}")
        if not isinstance(crate.get("features"), list) or not all(isinstance(x, str) for x in crate["features"]):
            raise VendorError(f"invalid features for {name}")
        for field in ("unsafe", "reviewer", "reason"):
            if not isinstance(crate.get(field), (bool, str)) or crate[field] in ("", None):
                raise VendorError(f"missing {field} for {name}")
        indexed[name] = crate
    return indexed


def verify(root: Path, *, lock_path: Path, approval_path: Path, locked: bool = False) -> int:
    approval = _validate_approved(_load(approval_path))
    lock = _load_lock(lock_path)
    packages = lock.get("package")
    if not isinstance(packages, list):
        raise VendorError("Cargo.lock package must be an array")
    seen: set[str] = set()
    for package in packages:
        if not isinstance(package, dict) or not isinstance(package.get("name"), str):
            raise VendorError("Cargo.lock package records must have names")
        name = package["name"]
        if name in seen:
            raise VendorError(f"duplicate locked crate: {name}")
        seen.add(name)
        record = approval.get(name)
        if record is None:
            raise VendorError(f"unapproved crate: {name}")
        if package.get("version") != record["version"]:
            raise VendorError(f"version mismatch for {name}")
        source = _package_source(record)
        vendor_path = root / source
        if not vendor_path.is_dir():
            raise VendorError(f"missing local crate directory for {name}")
        if _digest(vendor_path / "Cargo.toml") != record["checksum"]:
            raise VendorError(f"local crate checksum mismatch for {name}")
    extras = set(approval) - seen
    if extras:
        raise VendorError(f"approved crate is not locked: {sorted(extras)[0]}")
    if locked and lock.get("version") not in (3, 4):
        raise VendorError("locked verification requires Cargo.lock format version 3 or 4")
    return len(seen)


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--lock", type=Path, default=root / "rust-sdk" / "Cargo.lock")
    parser.add_argument("--approved", type=Path, default=root / "tools" / "rust" / "approved-crates.json")
    parser.add_argument("--locked", action="store_true")
    args = parser.parse_args()
    try:
        count = verify(args.root.resolve(), lock_path=args.lock.resolve(), approval_path=args.approved.resolve(), locked=args.locked)
    except VendorError as error:
        parser.error(str(error))
    print(f"verified {count} vendored Rust crates")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
