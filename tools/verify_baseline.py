#!/usr/bin/env python3
"""Verify the locked PoisonedOS materialized baseline tree."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys
from typing import Any, Iterable


SCHEMA = "poison.baseline/v1"
OFFICIAL_COMMIT = "a55e39395ff31bd5fdf3929c70720a7fb76e5968"
EXPECTED_DEPENDENCY_COUNT = 12
ALLOWED_CLASSIFICATIONS = {
    "upstream",
    "dependency",
    "poison-modified",
    "poison-added",
}
IGNORED_ROOT_PREFIXES = {
    ".cache",
    ".fbt",
    ".git",
    "build",
    "dist",
    "do_not_include",
    "toolchain",
}
IGNORED_PATH_PREFIXES = {
    "bridge/target",
    "dashboard/node_modules",
    "tools/protocol/target",
}
IGNORED_EXACT_PATHS = {
    "tools/hil/inventory.json",
}
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}\Z")
COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}\Z")


class BaselineError(ValueError):
    """Raised when the lock is malformed."""


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_lock(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise BaselineError(f"cannot load lock: {error}") from error
    if not isinstance(value, dict):
        raise BaselineError("lock root must be a JSON object")
    return value


def validate_relative_path(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise BaselineError(f"{field} must be a non-empty string")
    path = PurePosixPath(value)
    if path.is_absolute() or value != path.as_posix() or ".." in path.parts:
        raise BaselineError(f"{field} must be a normalized relative POSIX path: {value!r}")
    return value


def validate_lock(lock: dict[str, Any]) -> list[dict[str, str]]:
    required_strings = {
        "schema": SCHEMA,
        "upstream": "flipperdevices/flipperzero-firmware",
        "commit": OFFICIAL_COMMIT,
        "sourcePath": "do_not_include/flipperzero-firmware",
        "dependencyMode": "resolved-by-ADR-0003",
    }
    for field, expected in required_strings.items():
        if lock.get(field) != expected:
            raise BaselineError(f"{field} must equal {expected!r}")

    raw_files = lock.get("files")
    if not isinstance(raw_files, list) or not raw_files:
        raise BaselineError("files must be a non-empty array")

    files: list[dict[str, str]] = []
    seen_paths: set[str] = set()
    for index, raw_entry in enumerate(raw_files):
        if not isinstance(raw_entry, dict):
            raise BaselineError(f"files[{index}] must be an object")
        path = validate_relative_path(raw_entry.get("path"), f"files[{index}].path")
        if path in seen_paths:
            raise BaselineError(f"duplicate file path: {path}")
        seen_paths.add(path)
        classification = raw_entry.get("classification")
        if classification not in ALLOWED_CLASSIFICATIONS:
            raise BaselineError(f"invalid classification for {path}: {classification!r}")
        mode = raw_entry.get("mode")
        if mode not in {"100644", "100755", "120000"}:
            raise BaselineError(f"invalid mode for {path}: {mode!r}")
        digest = raw_entry.get("sha256")
        if not isinstance(digest, str) or SHA256_PATTERN.fullmatch(digest) is None:
            raise BaselineError(f"invalid sha256 for {path}")
        files.append(
            {
                "path": path,
                "classification": classification,
                "mode": mode,
                "sha256": digest,
            }
        )

    dependencies = lock.get("dependencies")
    if not isinstance(dependencies, list):
        raise BaselineError("dependencies must be an array")
    dependency_paths: set[str] = set()
    for index, dependency in enumerate(dependencies):
        if not isinstance(dependency, dict):
            raise BaselineError(f"dependencies[{index}] must be an object")
        path = validate_relative_path(
            dependency.get("path"), f"dependencies[{index}].path"
        )
        commit = dependency.get("commit")
        if not isinstance(commit, str) or COMMIT_PATTERN.fullmatch(commit) is None:
            raise BaselineError(f"invalid dependency commit for {path}")
        dependency_paths.add(path)
    if len(dependencies) != EXPECTED_DEPENDENCY_COUNT or len(dependency_paths) != len(
        dependencies
    ):
        raise BaselineError("dependencies must contain exactly 12 unique paths")

    expected_tree_digest = tree_digest(files)
    if lock.get("upstreamTreeSha256") != expected_tree_digest:
        raise BaselineError("upstreamTreeSha256 mismatch")
    return files


def tree_digest(files: Iterable[dict[str, str]]) -> str:
    records = "".join(
        f"{entry['path']}\0{entry['mode']}\0{entry['sha256']}\n"
        for entry in sorted(files, key=lambda item: item["path"])
    )
    return sha256_bytes(records.encode("utf-8"))


def is_ignored(path: str, lock_relative_path: str | None) -> bool:
    if path == lock_relative_path:
        return True
    parts = PurePosixPath(path).parts
    if not parts:
        return True
    if parts[0] in IGNORED_ROOT_PREFIXES:
        return True
    if "__pycache__" in parts or path.endswith((".pyc", ".pyo")):
        return True
    if path in IGNORED_EXACT_PATHS:
        return True
    return any(path == prefix or path.startswith(f"{prefix}/") for prefix in IGNORED_PATH_PREFIXES)


def file_mode(path: Path) -> str:
    metadata = path.lstat()
    if stat.S_ISLNK(metadata.st_mode):
        return "120000"
    if not stat.S_ISREG(metadata.st_mode):
        raise BaselineError(f"unsupported filesystem object: {path}")
    return "100755" if metadata.st_mode & 0o111 else "100644"


def file_digest(path: Path) -> str:
    if path.is_symlink():
        return sha256_bytes(os.readlink(path).encode("utf-8"))
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def scan_paths(root: Path, lock_path: Path) -> dict[str, Path]:
    try:
        lock_relative = lock_path.relative_to(root).as_posix()
    except ValueError:
        lock_relative = None

    paths: dict[str, Path] = {}
    for directory, directory_names, file_names in os.walk(root, followlinks=False):
        directory_path = Path(directory)
        relative_directory = directory_path.relative_to(root)

        retained_directories: list[str] = []
        for name in sorted(directory_names):
            child = directory_path / name
            relative = (relative_directory / name).as_posix()
            if is_ignored(relative, lock_relative):
                continue
            if child.is_symlink():
                paths[relative] = child
            else:
                retained_directories.append(name)
        directory_names[:] = retained_directories

        for name in sorted(file_names):
            child = directory_path / name
            relative = (relative_directory / name).as_posix()
            if not is_ignored(relative, lock_relative):
                paths[relative] = child
    return paths


def verify(root: Path, lock_path: Path) -> list[str]:
    lock = load_lock(lock_path)
    expected_entries = validate_lock(lock)
    expected = {entry["path"]: entry for entry in expected_entries}
    actual = scan_paths(root, lock_path)

    errors: list[str] = []
    for path in sorted(expected.keys() - actual.keys()):
        errors.append(f"missing path: {path}")
    for path in sorted(actual.keys() - expected.keys()):
        errors.append(f"extra path: {path}")
    for path in sorted(expected.keys() & actual.keys()):
        try:
            actual_mode = file_mode(actual[path])
            actual_digest = file_digest(actual[path])
        except (BaselineError, OSError, UnicodeError) as error:
            errors.append(f"cannot inspect {path}: {error}")
            continue
        if actual_mode != expected[path]["mode"]:
            errors.append(
                f"mode mismatch: {path} (expected {expected[path]['mode']}, got {actual_mode})"
            )
        if actual_digest != expected[path]["sha256"]:
            errors.append(f"content mismatch: {path}")
    return errors


def parse_arguments() -> argparse.Namespace:
    repository_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=repository_root)
    parser.add_argument(
        "--lock", type=Path, default=repository_root / "provenance" / "baseline.lock.json"
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    root = arguments.root.resolve()
    lock_path = arguments.lock.resolve()
    try:
        errors = verify(root, lock_path)
    except BaselineError as error:
        print(error, file=sys.stderr)
        return 1
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("baseline verification passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
