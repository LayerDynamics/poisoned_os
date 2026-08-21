#!/usr/bin/env python3
"""Classify every PoisonedOS product path against the locked OFW tree."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Any

from verify_baseline import file_digest, file_mode, scan_paths


SCHEMA = "poison.upstream-paths/v1"
CLASSIFICATIONS = (
    "identical",
    "poison-modified",
    "poison-added",
    "upstream-omitted",
    "dependency",
)


class ComparisonError(RuntimeError):
    """Raised when the upstream comparison cannot be produced."""


def run_git(repository: Path, *arguments: str, binary: bool = False) -> bytes | str:
    try:
        return subprocess.check_output(
            ["git", "-C", os.fspath(repository), *arguments],
            stderr=subprocess.PIPE,
            text=not binary,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        detail = getattr(error, "stderr", None)
        if isinstance(detail, bytes):
            detail = detail.decode("utf-8", errors="replace")
        raise ComparisonError((detail or str(error)).strip()) from error


def read_upstream_tree(source: Path) -> tuple[str, dict[str, dict[str, str]]]:
    commit = str(run_git(source, "rev-parse", "HEAD")).strip()
    raw_tree = run_git(source, "ls-tree", "-rz", "HEAD", binary=True)
    assert isinstance(raw_tree, bytes)
    entries: dict[str, dict[str, str]] = {}
    for record in raw_tree.split(b"\0"):
        if not record:
            continue
        try:
            metadata, encoded_path = record.split(b"\t", 1)
            mode, object_type, object_id = metadata.decode("ascii").split()
            path = encoded_path.decode("utf-8")
        except (UnicodeError, ValueError) as error:
            raise ComparisonError("git ls-tree returned an invalid record") from error
        entries[path] = {
            "mode": mode,
            "objectType": object_type,
            "objectId": object_id,
        }
    return commit, entries


def product_metadata(path: Path) -> dict[str, str]:
    return {"productMode": file_mode(path), "productSha256": file_digest(path)}


def upstream_metadata(source: Path, path: str, mode: str) -> dict[str, str]:
    source_path = source / path
    if not source_path.exists() and not source_path.is_symlink():
        raise ComparisonError(f"tracked upstream path is not materialized: {path}")
    return {"upstreamMode": mode, "upstreamSha256": file_digest(source_path)}


def dependency_owner(path: str, dependency_paths: tuple[str, ...]) -> str | None:
    for dependency_path in dependency_paths:
        if path == dependency_path or path.startswith(f"{dependency_path}/"):
            return dependency_path
    return None


def build_report(root: Path, source: Path, output: Path) -> dict[str, Any]:
    commit, upstream_tree = read_upstream_tree(source)
    dependencies = {
        path: entry
        for path, entry in upstream_tree.items()
        if entry["mode"] == "160000" and entry["objectType"] == "commit"
    }
    upstream_files = {
        path: entry for path, entry in upstream_tree.items() if path not in dependencies
    }
    dependency_paths = tuple(sorted(dependencies, key=len, reverse=True))
    product_paths = scan_paths(root, output)
    product_paths.pop("provenance/baseline.lock.json", None)
    entries: list[dict[str, Any]] = []

    for path, product_path in sorted(product_paths.items()):
        owner = dependency_owner(path, dependency_paths)
        if owner is not None:
            entries.append(
                {
                    "path": path,
                    "classification": "dependency",
                    "dependencyPath": owner,
                    "upstreamCommit": dependencies[owner]["objectId"],
                    **product_metadata(product_path),
                }
            )
            continue

        upstream_entry = upstream_files.get(path)
        if upstream_entry is None:
            entries.append(
                {
                    "path": path,
                    "classification": "poison-added",
                    **product_metadata(product_path),
                }
            )
            continue

        product = product_metadata(product_path)
        upstream = upstream_metadata(source, path, upstream_entry["mode"])
        differences: list[str] = []
        if product["productMode"] != upstream["upstreamMode"]:
            differences.append("mode")
        if product["productSha256"] != upstream["upstreamSha256"]:
            differences.append("content")
        classification = "poison-modified" if differences else "identical"
        entry: dict[str, Any] = {
            "path": path,
            "classification": classification,
            **product,
            **upstream,
        }
        if differences:
            entry["differences"] = differences
        entries.append(entry)

    for path, upstream_entry in sorted(upstream_files.items()):
        if path in product_paths:
            continue
        entries.append(
            {
                "path": path,
                "classification": "upstream-omitted",
                **upstream_metadata(source, path, upstream_entry["mode"]),
            }
        )

    existing_paths = {entry["path"] for entry in entries}
    for path, dependency in sorted(dependencies.items()):
        if path not in existing_paths:
            entries.append(
                {
                    "path": path,
                    "classification": "dependency",
                    "dependencyPath": path,
                    "upstreamCommit": dependency["objectId"],
                    "upstreamMode": "160000",
                }
            )

    entries.sort(key=lambda entry: entry["path"])
    summary = {
        classification: sum(
            entry["classification"] == classification for entry in entries
        )
        for classification in CLASSIFICATIONS
    }
    return {
        "schema": SCHEMA,
        "upstream": "flipperdevices/flipperzero-firmware",
        "baselineCommit": commit,
        "sourcePath": source.relative_to(root).as_posix()
        if source.is_relative_to(root)
        else os.fspath(source),
        "dependencyMode": "fully-vendored",
        "summary": summary,
        "paths": entries,
    }


def canonical_json(value: dict[str, Any]) -> str:
    return json.dumps(value, indent=2, sort_keys=True) + "\n"


def parse_arguments() -> argparse.Namespace:
    repository_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=repository_root)
    parser.add_argument(
        "--baseline",
        type=Path,
        default=repository_root / "do_not_include" / "flipperzero-firmware",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=repository_root / "provenance" / "upstream-paths.json",
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--stdout", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    root = arguments.root.resolve()
    source = arguments.baseline.resolve()
    output = arguments.output.resolve()
    try:
        rendered = canonical_json(build_report(root, source, output))
    except (ComparisonError, OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1

    if arguments.stdout:
        sys.stdout.write(rendered)
        return 0
    if arguments.write:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered, encoding="utf-8")
        print(f"wrote {output}")
        return 0

    try:
        current = output.read_text(encoding="utf-8")
    except OSError as error:
        print(f"cannot read upstream path report: {error}", file=sys.stderr)
        return 1
    if current != rendered:
        print("upstream path report is stale", file=sys.stderr)
        return 1
    print("upstream comparison passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
