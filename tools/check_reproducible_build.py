#!/usr/bin/env python3
"""Build PoisonedOS twice in clean roots and compare required payload artifacts."""

from __future__ import annotations

import argparse
import hashlib
from itertools import zip_longest
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile

from verify_baseline import scan_paths
from verify_toolchain import ToolchainError, verify as verify_toolchain


REQUIRED_FAMILIES = ("firmware", "updater", "resources", "api", "map")
PAYLOAD_SUFFIXES = {".bin", ".dfu", ".elf", ".hex"}


class ReproducibleBuildError(RuntimeError):
    """Raised when clean builds or their payload artifacts differ."""


def sha256(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def artifact_family(relative: str) -> str | None:
    path = Path(relative)
    lowered = relative.lower()
    name = path.name.lower()
    if path.parts[:3] == ("build", "f7-firmware-D", "resources"):
        return "resources"
    if relative == "build/f7-firmware-D/assets/compiled/firmware_api_table.h":
        return "api"
    if path.suffix == ".map" and len(path.parts) == 3:
        return "map"
    if (
        len(path.parts) == 3
        and "f7-firmware" in lowered
        and name.startswith("firmware")
    ):
        return "firmware" if path.suffix in PAYLOAD_SUFFIXES else None
    if len(path.parts) == 3 and "f7-updater" in lowered and name.startswith("updater"):
        return "updater" if path.suffix in PAYLOAD_SUFFIXES else None
    return None


def collect_artifacts(root: Path) -> dict[str, tuple[str, str]]:
    build = root / "build"
    if not build.is_dir():
        raise ReproducibleBuildError(f"missing build directory: {build}")
    artifacts: dict[str, tuple[str, str]] = {}
    families: set[str] = set()
    for path in sorted(item for item in build.rglob("*") if item.is_file()):
        relative = path.relative_to(root).as_posix()
        family = artifact_family(relative)
        if family is None:
            continue
        mode = "100755" if path.stat().st_mode & stat.S_IXUSR else "100644"
        artifacts[relative] = (mode, sha256(path))
        families.add(family)
    missing = set(REQUIRED_FAMILIES) - families
    if missing:
        raise ReproducibleBuildError(
            "missing required artifact families: " + ", ".join(sorted(missing))
        )
    return artifacts


def first_text_difference(left: Path, right: Path) -> str:
    try:
        left_lines = left.read_text(encoding="utf-8").splitlines()
        right_lines = right.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError):
        return ""
    for line_number, (left_line, right_line) in enumerate(
        zip_longest(left_lines, right_lines, fillvalue="<missing>"), start=1
    ):
        if left_line != right_line:
            return (
                f"; first differing line {line_number}: "
                f"left={left_line[:200]!r}, right={right_line[:200]!r}"
            )
    return ""


def compare(left: Path, right: Path) -> tuple[int, str]:
    left_artifacts = collect_artifacts(left)
    right_artifacts = collect_artifacts(right)
    left_paths = set(left_artifacts)
    right_paths = set(right_artifacts)
    if missing := sorted(left_paths - right_paths):
        raise ReproducibleBuildError(f"artifact missing from right build: {missing[0]}")
    if extra := sorted(right_paths - left_paths):
        raise ReproducibleBuildError(f"artifact missing from left build: {extra[0]}")
    for path in sorted(left_paths):
        left_mode, left_digest = left_artifacts[path]
        right_mode, right_digest = right_artifacts[path]
        if left_mode != right_mode:
            raise ReproducibleBuildError(f"artifact mode mismatch: {path}")
        if left_digest != right_digest:
            detail = first_text_difference(left / path, right / path)
            raise ReproducibleBuildError(f"artifact content mismatch: {path}{detail}")
    records = "".join(
        f"{path}\0{left_artifacts[path][0]}\0{left_artifacts[path][1]}\n"
        for path in sorted(left_paths)
    )
    return len(left_paths), hashlib.sha256(records.encode("utf-8")).hexdigest()


def copy_product_tree(source: Path, destination: Path) -> None:
    lock = source / "provenance" / "baseline.lock.json"
    for relative, path in scan_paths(source, lock).items():
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if path.is_symlink():
            target.symlink_to(os.readlink(path))
        else:
            shutil.copy2(path, target)
    target_lock = destination / "provenance" / "baseline.lock.json"
    target_lock.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(lock, target_lock)


def run_checked(command: list[str], cwd: Path, environment: dict[str, str]) -> None:
    result = subprocess.run(
        command,
        cwd=cwd,
        capture_output=True,
        check=False,
        env=environment,
        text=True,
    )
    if result.returncode != 0:
        output = (result.stdout + result.stderr)[-8000:]
        raise ReproducibleBuildError(
            f"command failed ({' '.join(command)}):\n{output}"
        )


def initialize_snapshot(path: Path) -> None:
    environment = os.environ.copy()
    environment.update(
        {
            "GIT_AUTHOR_DATE": "2000-01-01T00:00:00Z",
            "GIT_COMMITTER_DATE": "2000-01-01T00:00:00Z",
        }
    )
    run_checked(["git", "init", "-q", "-b", "main"], path, environment)
    run_checked(["git", "add", "-f", "--all"], path, environment)
    run_checked(
        [
            "git",
            "-c",
            "user.name=PoisonedOS Reproducible Build",
            "-c",
            "user.email=reproducible@poisoned.invalid",
            "commit",
            "-q",
            "-m",
            "deterministic source snapshot",
        ],
        path,
        environment,
    )


def build_twice(root: Path, manifest: Path) -> tuple[int, str]:
    try:
        verify_toolchain(root, manifest)
    except ToolchainError as error:
        raise ReproducibleBuildError(str(error)) from error
    with tempfile.TemporaryDirectory(prefix="poison-repro-a-") as first_directory:
        with tempfile.TemporaryDirectory(prefix="poison-repro-b-") as second_directory:
            snapshots = (Path(first_directory), Path(second_directory))
            for snapshot in snapshots:
                copy_product_tree(root, snapshot)
                initialize_snapshot(snapshot)
                environment = os.environ.copy()
                environment.update(
                    {
                        "DIST_SUFFIX": "poisonedos",
                        "FBT_NO_SYNC": "1",
                        "FBT_TOOLCHAIN_PATH": os.fspath(root),
                        "SOURCE_DATE_EPOCH": "946684800",
                    }
                )
                environment.pop("FBT_NOENV", None)
                run_checked(
                    [
                        os.fspath(snapshot / "fbt"),
                        "firmware_all",
                        "updater_all",
                        "resources",
                    ],
                    snapshot,
                    environment,
                )
            return compare(*snapshots)


def parse_arguments() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument(
        "--manifest", type=Path, default=root / "toolchains" / "manifest.lock.json"
    )
    parser.add_argument("--left", type=Path)
    parser.add_argument("--right", type=Path)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if (arguments.left is None) != (arguments.right is None):
        print("--left and --right must be provided together", file=sys.stderr)
        return 2
    try:
        if arguments.left is not None:
            count, tree_digest = compare(
                arguments.left.resolve(), arguments.right.resolve()
            )
        else:
            count, tree_digest = build_twice(
                arguments.root.resolve(), arguments.manifest.resolve()
            )
    except (OSError, ReproducibleBuildError) as error:
        print(error, file=sys.stderr)
        return 1
    print(
        f"reproducible build comparison passed: {count} artifacts, "
        f"tree sha256 {tree_digest}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
