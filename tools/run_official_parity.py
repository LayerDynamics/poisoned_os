#!/usr/bin/env python3
"""Run locked OFW/PoisonedOS host parity gates and classify expected deltas."""

from __future__ import annotations

import argparse
import ast
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
from typing import Any


EXPECTED_API_ADDITION = {
    "entry": "Function",
    "status": "+",
    "name": "menu_set_header",
    "type": "void",
    "params": "Menu*, const char*",
}
EXPECTED_BUILD_OPTIONS = {
    "FIRMWARE_ORIGIN": {"baseline": "Official", "product": "PoisonedOS"},
    "DIST_SUFFIX": {"baseline": "local", "product": "poisonedos"},
}
METADATA_FIELDS = {
    "firmware_build_date",
    "firmware_commit",
    "firmware_branch",
    "firmware_target",
}
ARTIFACT_SPECS = {
    "firmware_bin": ("build/f7-firmware-D/firmware.bin", "bin"),
    "firmware_dfu": ("build/f7-firmware-D/firmware.dfu", "dfu"),
    "firmware_elf": ("build/f7-firmware-D/firmware.elf", "elf"),
    "firmware_json": ("build/f7-firmware-D/firmware.json", "json"),
    "updater_bin": ("build/f7-updater-D/updater.bin", "bin"),
    "updater_dfu": ("build/f7-updater-D/updater.dfu", "dfu"),
    "updater_elf": ("build/f7-updater-D/updater.elf", "elf"),
    "updater_json": ("build/f7-updater-D/updater.json", "json"),
}
COMMANDS = (
    ("lint", ("lint_all",)),
    ("production", ("firmware_all", "updater_all", "resources")),
    ("unit-image", ("FIRMWARE_APP_SET=unit_tests", "firmware_all")),
    ("production-restore", ("firmware_all", "updater_all", "resources")),
)
SOURCE_CLASSIFICATIONS = {
    "identical",
    "dependency",
    "poison-added",
    "poison-modified",
    "upstream-omitted",
}


class OfficialParityError(RuntimeError):
    """Raised when an OFW/Product difference has no approved classification."""


def sha256(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def load_api(path: Path) -> tuple[str, dict[tuple[str, str], dict[str, str]]]:
    try:
        with path.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream))
    except (OSError, UnicodeError, csv.Error) as error:
        raise OfficialParityError(f"cannot read API table {path}: {error}") from error
    versions = [row for row in rows if row.get("entry") == "Version"]
    if len(versions) != 1:
        raise OfficialParityError(f"API table must have one version: {path}")
    entries: dict[tuple[str, str], dict[str, str]] = {}
    for row in rows:
        if row.get("entry") == "Version":
            continue
        key = (row.get("entry", ""), row.get("name", ""))
        if key in entries:
            raise OfficialParityError(f"duplicate API entry: {key[0]}:{key[1]}")
        entries[key] = row
    return versions[0]["name"], entries


def compare_api_tables(product: Path, baseline: Path) -> dict[str, Any]:
    product_version, product_entries = load_api(product)
    baseline_version, baseline_entries = load_api(baseline)
    try:
        baseline_major, baseline_minor = map(int, baseline_version.split("."))
        product_major, product_minor = map(int, product_version.split("."))
    except ValueError as error:
        raise OfficialParityError("invalid API version") from error
    if (product_major, product_minor) != (baseline_major, baseline_minor + 1):
        raise OfficialParityError(
            f"unexplained API version delta: {baseline_version} -> {product_version}"
        )
    missing = sorted(set(baseline_entries) - set(product_entries))
    if missing:
        kind, name = missing[0]
        raise OfficialParityError(f"unexplained API removal: {kind}:{name}")
    for key in sorted(set(baseline_entries) & set(product_entries)):
        if product_entries[key] != baseline_entries[key]:
            raise OfficialParityError(
                f"unexplained API signature/status change: {key[0]}:{key[1]}"
            )
    additions = sorted(set(product_entries) - set(baseline_entries))
    expected_key = (
        EXPECTED_API_ADDITION["entry"],
        EXPECTED_API_ADDITION["name"],
    )
    if additions != [expected_key]:
        label = f"{additions[0][0]}:{additions[0][1]}" if additions else "<none>"
        raise OfficialParityError(f"unexplained API addition: {label}")
    if product_entries[expected_key] != EXPECTED_API_ADDITION:
        raise OfficialParityError("approved API addition has an unexpected signature")
    return {
        "baseline_version": baseline_version,
        "product_version": product_version,
        "approved_additions": [f"{expected_key[0]}:{expected_key[1]}"],
    }


def literal_assignments(path: Path) -> dict[str, Any]:
    try:
        tree = ast.parse(path.read_text(encoding="utf-8"), filename=os.fspath(path))
    except (OSError, UnicodeError, SyntaxError) as error:
        raise OfficialParityError(
            f"cannot parse build options {path}: {error}"
        ) from error
    assignments: dict[str, Any] = {}
    for node in tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1:
            continue
        target = node.targets[0]
        if isinstance(target, ast.Name):
            try:
                assignments[target.id] = ast.literal_eval(node.value)
            except (ValueError, TypeError):
                continue
    return assignments


def normalized_build_options(path: Path) -> str:
    try:
        content = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise OfficialParityError(
            f"cannot read build options {path}: {error}"
        ) from error
    for name in EXPECTED_BUILD_OPTIONS:
        pattern = re.compile(rf"(?m)^{name}\s*=\s*[^\n]+$")
        content, count = pattern.subn(f'{name} = "@classified@"', content)
        if count != 1:
            raise OfficialParityError(
                f"build option {name} must be assigned exactly once"
            )
    return content


def compare_build_options(product: Path, baseline: Path) -> dict[str, dict[str, str]]:
    product_values = literal_assignments(product)
    baseline_values = literal_assignments(baseline)
    for name, expected in EXPECTED_BUILD_OPTIONS.items():
        if baseline_values.get(name) != expected["baseline"]:
            raise OfficialParityError(f"unexpected baseline {name}")
        if product_values.get(name) != expected["product"]:
            raise OfficialParityError(f"unexpected product {name}")
    if normalized_build_options(product) != normalized_build_options(baseline):
        raise OfficialParityError("unexplained fbt_options difference")
    return EXPECTED_BUILD_OPTIONS


def proto_digests(root: Path) -> dict[str, str]:
    directory = root / "assets" / "protobuf"
    paths = sorted(directory.rglob("*.proto"))
    if not paths:
        raise OfficialParityError(f"no protobuf sources found in {directory}")
    return {path.relative_to(directory).as_posix(): sha256(path) for path in paths}


def compare_protobuf(product: Path, baseline: Path) -> int:
    product_digests = proto_digests(product)
    baseline_digests = proto_digests(baseline)
    if product_digests != baseline_digests:
        changed = sorted(set(product_digests) | set(baseline_digests))
        for path in changed:
            if product_digests.get(path) != baseline_digests.get(path):
                raise OfficialParityError(f"unexplained protobuf difference: {path}")
    return len(product_digests)


def validate_source_report(path: Path, commit: str) -> dict[str, int]:
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise OfficialParityError(
            f"cannot read source classification report: {error}"
        ) from error
    if not isinstance(report, dict) or report.get("baselineCommit") != commit:
        raise OfficialParityError("source classification baseline commit mismatch")
    paths = report.get("paths")
    if not isinstance(paths, list) or not paths:
        raise OfficialParityError("source classification report has no paths")
    counts: dict[str, int] = {}
    seen: set[str] = set()
    for entry in paths:
        if not isinstance(entry, dict):
            raise OfficialParityError("invalid source classification entry")
        source_path = entry.get("path")
        classification = entry.get("classification")
        if not isinstance(source_path, str) or not source_path or source_path in seen:
            raise OfficialParityError("invalid or duplicate source classification path")
        seen.add(source_path)
        if classification not in SOURCE_CLASSIFICATIONS:
            raise OfficialParityError(
                f"invalid source classification for {source_path}: {classification!r}"
            )
        counts[classification] = counts.get(classification, 0) + 1
    if not counts.get("poison-modified"):
        raise OfficialParityError("source report has no product modifications")
    return dict(sorted(counts.items()))


def load_metadata(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise OfficialParityError(
            f"invalid artifact metadata {path}: {error}"
        ) from error
    if not isinstance(value, dict) or set(value) != METADATA_FIELDS:
        raise OfficialParityError(f"unexplained artifact metadata fields: {path.name}")
    if value.get("firmware_target") != 7:
        raise OfficialParityError(f"firmware target mismatch: {path.name}")
    return value


def artifact_path(directory: Path, logical_name: str) -> Path:
    template, _ = ARTIFACT_SPECS[logical_name]
    return directory / template


def validate_binary(path: Path, kind: str) -> None:
    try:
        prefix = path.read_bytes()[:8]
    except OSError as error:
        raise OfficialParityError(f"missing artifact {path}: {error}") from error
    if len(prefix) < 5:
        raise OfficialParityError(f"artifact is empty or truncated: {path.name}")
    if kind == "dfu" and not prefix.startswith(b"DfuSe"):
        raise OfficialParityError(f"invalid DFU magic: {path.name}")
    if kind == "elf" and not prefix.startswith(b"\x7fELF"):
        raise OfficialParityError(f"invalid ELF magic: {path.name}")


def compare_artifacts(product: Path, baseline: Path) -> dict[str, dict[str, Any]]:
    report: dict[str, dict[str, Any]] = {}
    for logical_name, (_, kind) in ARTIFACT_SPECS.items():
        product_path = artifact_path(product, logical_name)
        baseline_path = artifact_path(baseline, logical_name)
        if kind == "json":
            product_metadata = load_metadata(product_path)
            baseline_metadata = load_metadata(baseline_path)
            if (
                baseline_metadata["firmware_target"]
                != product_metadata["firmware_target"]
            ):
                raise OfficialParityError(f"firmware target mismatch: {logical_name}")
            report[logical_name] = {
                "classification": "build-metadata",
                "product": product_metadata,
                "baseline": baseline_metadata,
            }
            continue
        validate_binary(product_path, kind)
        validate_binary(baseline_path, kind)
        report[logical_name] = {
            "classification": "product-source-delta",
            "product_sha256": sha256(product_path),
            "baseline_sha256": sha256(baseline_path),
            "product_size": product_path.stat().st_size,
            "baseline_size": baseline_path.stat().st_size,
        }
    return report


def run_checked(
    label: str, root: Path, arguments: tuple[str, ...], environment: dict[str, str]
) -> None:
    command = [os.fspath(root / "fbt"), *arguments]
    print(f"[{label}] {' '.join(arguments)}", flush=True)
    result = subprocess.run(
        command,
        cwd=root,
        capture_output=True,
        check=False,
        env=environment,
        text=True,
    )
    if result.returncode != 0:
        output = (result.stdout + result.stderr)[-12000:]
        raise OfficialParityError(
            f"{label} command failed ({' '.join(arguments)}):\n{output}"
        )
    print(f"[{label}] passed", flush=True)


def run_python_check(root: Path, script: str, *arguments: str) -> None:
    command = [sys.executable, os.fspath(root / script), *arguments]
    result = subprocess.run(
        command,
        cwd=root,
        capture_output=True,
        check=False,
        text=True,
    )
    if result.returncode != 0:
        output = (result.stdout + result.stderr)[-12000:]
        raise OfficialParityError(f"source comparison check failed:\n{output}")


def verify_commit(root: Path, baseline: Path) -> str:
    try:
        lock = json.loads(
            (root / "provenance/baseline.lock.json").read_text(encoding="utf-8")
        )
        expected = lock["commit"]
    except (OSError, KeyError, UnicodeError, json.JSONDecodeError) as error:
        raise OfficialParityError(f"cannot load baseline commit: {error}") from error
    result = subprocess.run(
        ["git", "-C", os.fspath(baseline), "rev-parse", "HEAD"],
        capture_output=True,
        check=False,
        text=True,
    )
    actual = result.stdout.strip()
    if result.returncode != 0 or actual != expected:
        raise OfficialParityError(
            f"baseline commit mismatch: expected {expected}, got {actual or '<unreadable>'}"
        )
    status = subprocess.run(
        [
            "git",
            "-C",
            os.fspath(baseline),
            "status",
            "--porcelain",
            "--untracked-files=no",
        ],
        capture_output=True,
        check=False,
        text=True,
    )
    if status.returncode != 0 or status.stdout:
        raise OfficialParityError("baseline has tracked working-tree changes")
    return expected


def run_matrix(root: Path, baseline: Path) -> list[dict[str, str]]:
    environment = os.environ.copy()
    environment.update(
        {
            "FBT_NO_SYNC": "1",
            "FBT_TOOLCHAIN_PATH": os.fspath(root),
            "SOURCE_DATE_EPOCH": "946684800",
        }
    )
    results: list[dict[str, str]] = []
    for label, checkout in (("PoisonedOS", root), ("OFW", baseline)):
        for phase, arguments in COMMANDS:
            run_checked(label, checkout, arguments, environment)
            results.append(
                {
                    "checkout": label,
                    "phase": phase,
                    "command": "./fbt " + " ".join(arguments),
                    "result": "pass",
                }
            )
    return results


def parse_arguments() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument(
        "--baseline", type=Path, default=root / "do_not_include/flipperzero-firmware"
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    root = arguments.root.resolve()
    baseline = arguments.baseline.resolve()
    try:
        commit = verify_commit(root, baseline)
        run_python_check(
            root,
            "tools/compare_upstream.py",
            "--check",
            "--baseline",
            os.fspath(baseline),
        )
        source_classifications = validate_source_report(
            root / "provenance/upstream-paths.json", commit
        )
        api = compare_api_tables(
            root / "targets/f7/api_symbols.csv",
            baseline / "targets/f7/api_symbols.csv",
        )
        options = compare_build_options(
            root / "fbt_options.py", baseline / "fbt_options.py"
        )
        proto_count = compare_protobuf(root, baseline)
        commands = run_matrix(root, baseline)
        artifacts = compare_artifacts(root, baseline)
    except (OSError, OfficialParityError) as error:
        print(error, file=sys.stderr)
        return 1
    summary = {
        "baseline_commit": commit,
        "hardware": {
            "host": platform.platform(),
            "architecture": platform.machine(),
            "firmware_target": "f7",
            "physical_device_used": False,
        },
        "api": api,
        "build_options": options,
        "protobuf_files": proto_count,
        "source_classifications": source_classifications,
        "commands": commands,
        "artifacts": artifacts,
    }
    print(json.dumps(summary, indent=2, sort_keys=True))
    print("official parity passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
