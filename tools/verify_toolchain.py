#!/usr/bin/env python3
"""Verify the exact PoisonedOS FBT toolchain bundle and build inputs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import subprocess
import sys
import tempfile
from typing import Any


SCHEMA = "poison.toolchain/v1"
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}\Z")


class ToolchainError(ValueError):
    """Raised when the configured build toolchain is not the locked bundle."""


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                hasher.update(chunk)
    except OSError as error:
        raise ToolchainError(f"cannot read {path}: {error}") from error
    return hasher.hexdigest()


def relative_path(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise ToolchainError(f"{field} must be a non-empty string")
    path = PurePosixPath(value)
    if path.is_absolute() or value != path.as_posix() or ".." in path.parts:
        raise ToolchainError(f"{field} must be a normalized relative path")
    return value


def expected_digest(value: Any, field: str) -> str:
    if not isinstance(value, str) or SHA256_PATTERN.fullmatch(value) is None:
        raise ToolchainError(f"{field} must be a SHA-256 digest")
    return value


def current_host() -> str:
    system = "darwin" if sys.platform == "darwin" else sys.platform
    return f"{platform.machine()}-{system}"


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ToolchainError(f"cannot load manifest: {error}") from error
    if not isinstance(manifest, dict) or manifest.get("schema") != SCHEMA:
        raise ToolchainError(f"toolchain schema must equal {SCHEMA}")
    return manifest


def verify_file(path: Path, locked_digest: str, mismatch: str, missing: str) -> None:
    if not path.is_file():
        raise ToolchainError(missing)
    if digest(path) != locked_digest:
        raise ToolchainError(mismatch)


def _build_inputs(manifest: dict[str, Any]) -> list[tuple[dict[str, Any], str]]:
    build_inputs = manifest.get("buildInputs")
    if not isinstance(build_inputs, list) or not build_inputs:
        raise ToolchainError("buildInputs must be a non-empty array")
    paths: set[str] = set()
    validated: list[tuple[dict[str, Any], str]] = []
    for build_input in build_inputs:
        if not isinstance(build_input, dict):
            raise ToolchainError("build input entries must be objects")
        path = relative_path(build_input.get("path"), "buildInputs.path")
        if path in paths:
            raise ToolchainError(f"duplicate build input: {path}")
        paths.add(path)
        expected_digest(build_input.get("sha256"), f"buildInputs.{path}.sha256")
        validated.append((build_input, path))
    return validated


def verify(
    root: Path,
    manifest_path: Path,
    *,
    verify_build_inputs: bool = True,
) -> tuple[str, int, int]:
    if os.environ.get("FBT_NOENV"):
        raise ToolchainError("FBT_NOENV host fallback is forbidden")

    manifest = load_manifest(manifest_path)
    bundle = manifest.get("bundle")
    if not isinstance(bundle, dict):
        raise ToolchainError("bundle must be an object")
    for field in ("version", "host", "path", "archivePath", "archiveUrl", "archiveSha256"):
        if field not in bundle:
            raise ToolchainError(f"bundle missing required field: {field}")
    if bundle["host"] != current_host():
        raise ToolchainError(
            f"wrong host toolchain: expected {bundle['host']}, found {current_host()}"
        )
    if not isinstance(bundle["archiveUrl"], str) or not bundle["archiveUrl"].startswith(
        "https://"
    ):
        raise ToolchainError("bundle archiveUrl must use HTTPS")

    bundle_path = root / relative_path(bundle["path"], "bundle.path")
    version_path = bundle_path / "VERSION"
    try:
        installed_version = version_path.read_text(encoding="utf-8").strip()
    except OSError as error:
        raise ToolchainError(f"missing bundle version: {version_path}") from error
    if installed_version != bundle["version"]:
        raise ToolchainError(
            f"bundle version mismatch: expected {bundle['version']}, found {installed_version}"
        )

    archive_path = root / relative_path(bundle["archivePath"], "bundle.archivePath")
    verify_file(
        archive_path,
        expected_digest(bundle["archiveSha256"], "bundle.archiveSha256"),
        "archive digest mismatch",
        f"missing toolchain archive: {archive_path}",
    )

    tools = manifest.get("tools")
    if not isinstance(tools, list) or not tools:
        raise ToolchainError("tools must be a non-empty array")
    names: set[str] = set()
    environment = os.environ.copy()
    environment.update(
        {
            "LC_ALL": "C",
            "PATH": os.pathsep.join((os.fspath(bundle_path / "bin"), "/usr/bin", "/bin")),
            "PYTHONHOME": os.fspath(bundle_path),
            "PYTHONNOUSERSITE": "1",
            "PYTHONPATH": "",
        }
    )
    for tool in tools:
        if not isinstance(tool, dict):
            raise ToolchainError("tool entries must be objects")
        name = tool.get("name")
        if not isinstance(name, str) or not name or name in names:
            raise ToolchainError(f"invalid or duplicate tool name: {name!r}")
        names.add(name)
        tool_path = bundle_path / relative_path(tool.get("path"), f"tools.{name}.path")
        verify_file(
            tool_path,
            expected_digest(tool.get("sha256"), f"tools.{name}.sha256"),
            f"tool digest mismatch: {name}",
            f"missing tool: {name}",
        )
        command = tool.get("versionCommand")
        pattern = tool.get("versionPattern")
        if (
            not isinstance(command, list)
            or not command
            or not all(isinstance(argument, str) for argument in command)
            or not isinstance(pattern, str)
            or not pattern
        ):
            raise ToolchainError(f"invalid version check: {name}")
        executable = bundle_path / relative_path(command[0], f"tools.{name}.versionCommand")
        try:
            result = subprocess.run(
                [os.fspath(executable), *command[1:]],
                capture_output=True,
                check=False,
                env=environment,
                text=True,
                timeout=30,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise ToolchainError(f"cannot run version check: {name}: {error}") from error
        output = result.stdout + result.stderr
        if result.returncode != 0 or re.search(pattern, output) is None:
            raise ToolchainError(f"tool version mismatch: {name}")

    build_inputs = _build_inputs(manifest)
    if verify_build_inputs:
        for build_input, path in build_inputs:
            verify_file(
                root / path,
                build_input["sha256"],
                f"build input digest mismatch: {path}",
                f"missing build input: {path}",
            )
    return bundle["version"], len(tools), len(build_inputs)


def refresh_build_input_lock(root: Path, manifest_path: Path) -> int:
    """Refresh declared source digests after revalidating the pinned toolchain."""

    manifest = load_manifest(manifest_path)
    verify(root, manifest_path, verify_build_inputs=False)
    build_inputs = _build_inputs(manifest)
    for build_input, path in build_inputs:
        source = root / path
        if not source.is_file():
            raise ToolchainError(f"missing build input: {path}")
        build_input["sha256"] = digest(source)

    manifest_path = manifest_path.resolve()
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=manifest_path.parent,
            prefix=f".{manifest_path.name}.",
            suffix=".tmp",
            delete=False,
        ) as stream:
            temporary_path = Path(stream.name)
            json.dump(manifest, stream, indent=2, sort_keys=True)
            stream.write("\n")
        verify(root, temporary_path)
        os.replace(temporary_path, manifest_path)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
    return len(build_inputs)


def parse_arguments() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument(
        "--manifest", type=Path, default=root / "toolchains" / "manifest.lock.json"
    )
    parser.add_argument(
        "--write",
        action="store_true",
        help="refresh only the declared build-input digests after validation",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        if arguments.write:
            count = refresh_build_input_lock(
                arguments.root.resolve(), arguments.manifest.resolve()
            )
            print(f"toolchain build-input lock refreshed: {count} inputs")
            return 0
        version, tool_count, input_count = verify(
            arguments.root.resolve(), arguments.manifest.resolve()
        )
    except ToolchainError as error:
        print(error, file=sys.stderr)
        return 1
    print(
        f"toolchain verification passed: bundle {version}, {tool_count} tools, "
        f"{input_count} build inputs"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
