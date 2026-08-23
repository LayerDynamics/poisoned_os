#!/usr/bin/env python3
"""Build and publishable-package a Poisoned_Os firmware release."""

from __future__ import annotations

import argparse
import base64
import binascii
import json
import os
from pathlib import Path
import subprocess

import build_web_installer_distribution as distribution


class ReleaseArtifactError(ValueError):
    """Raised when a release artifact build cannot produce a complete package."""


def _private_key_from_environment(name: str) -> bytes:
    encoded = os.environ.get(name)
    if not encoded:
        raise ReleaseArtifactError(f"missing {name}")
    try:
        key = base64.b64decode(encoded, validate=True)
    except (ValueError, binascii.Error) as error:
        raise ReleaseArtifactError(f"{name} is not valid base64") from error
    if not key:
        raise ReleaseArtifactError(f"{name} is empty")
    return key


def _run_firmware_build(root: Path, version: str, env: dict[str, str]) -> Path:
    command = [
        "./fbt",
        "DEBUG=0",
        "COMPACT=1",
        "DIST_SUFFIX=poisonedos",
        f"UPDATE_VERSION_STRING={version}",
        "updater_package",
    ]
    subprocess.run(command, cwd=root, env=env, check=True)
    package = root / "dist" / "f7-C" / "flipper-z-f7-update-poisonedos.tgz"
    if not package.is_file() or package.stat().st_size == 0:
        raise ReleaseArtifactError(f"firmware build did not produce {package}")
    return package


def build_release(
    root: Path,
    output: Path,
    version: str,
    channel: str,
    key_id: str,
    private_key: bytes,
    *,
    openssl: str = "openssl",
    run_build: bool = True,
) -> dict:
    if not distribution.VERSION.fullmatch(version):
        raise ReleaseArtifactError("release version must use MAJOR.MINOR.PATCH")
    if channel not in {"stable", "beta", "developer", "internal"}:
        raise ReleaseArtifactError("release channel is invalid")
    root = root.resolve()
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if run_build:
        environment = os.environ.copy()
        environment.setdefault("FBT_NO_SYNC", "1")
        environment.setdefault("SOURCE_DATE_EPOCH", "0")
        package = _run_firmware_build(root, version, environment)
    else:
        package = root / "dist" / "f7-C" / "flipper-z-f7-update-poisonedos.tgz"
        if not package.is_file():
            raise ReleaseArtifactError(f"test package does not exist: {package}")
    manifest = distribution.assemble(root, package, output, version, channel, key_id, private_key, openssl)
    (output / "build-info.json").write_text(
        json.dumps({
            "schema": "poison.release-build-info/v1",
            "version": version,
            "channel": channel,
            "package": package.name,
            "packageBytes": package.stat().st_size,
            "manifest": "release.json",
            "feed": "releases.json",
            "installerConfig": "installer-config.json",
        }, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--channel", default="developer")
    parser.add_argument("--key-id", required=True)
    parser.add_argument("--private-key-base64-env", default="POISON_RELEASE_PRIVATE_KEY_B64")
    parser.add_argument("--openssl", default="openssl")
    args = parser.parse_args()
    try:
        manifest = build_release(
            args.root,
            args.output,
            args.version,
            args.channel,
            args.key_id,
            _private_key_from_environment(args.private_key_base64_env),
            openssl=args.openssl,
        )
    except (OSError, ReleaseArtifactError, subprocess.SubprocessError) as error:
        parser.error(str(error))
    print(json.dumps({"version": manifest["version"], "channel": manifest["channel"], "output": str(args.output)}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
