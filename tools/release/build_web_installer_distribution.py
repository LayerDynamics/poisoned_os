#!/usr/bin/env python3
"""Assemble the signed firmware feed and immutable package for the web installer."""

from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

import build_web_installer_feed as feed_builder
import sign_release


VERSION = re.compile(r"^\d+\.\d+\.\d+$")
KEY_ID = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")
MAX_PACKAGE_BYTES = 32 * 1024 * 1024
CONFIG_SCHEMA = "poison.web-installer-config/v1"


class DistributionError(ValueError):
    """Raised when a Railway installer distribution cannot be assembled safely."""


def _private_key_from_environment(name: str) -> bytes:
    encoded = __import__("os").environ.get(name)
    if not encoded:
        raise DistributionError(f"missing {name}; Railway must provide the release signer")
    try:
        key = base64.b64decode(encoded, validate=True)
    except (ValueError, binascii.Error) as error:
        raise DistributionError(f"{name} is not valid base64") from error
    if not key:
        raise DistributionError(f"{name} is empty")
    return key


def _public_key(private_key: Path, openssl: str, output: Path) -> None:
    result = subprocess.run(
        [openssl, "pkey", "-in", str(private_key), "-pubout", "-out", str(output)],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise DistributionError(result.stderr.strip() or "release private key is invalid")


def _validate_key(private_key: Path, openssl: str) -> None:
    result = subprocess.run(
        [openssl, "pkey", "-in", str(private_key), "-text", "-noout"],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0 or ("ASN1 OID: prime256v1" not in result.stdout and "NIST CURVE: P-256" not in result.stdout):
        raise DistributionError("release signer must be an ECDSA P-256 private key")


def _manifest(root: Path, package: Path, version: str, channel: str) -> dict:
    try:
        relative = package.resolve().relative_to(root.resolve()).as_posix()
    except ValueError as error:
        raise DistributionError(f"firmware package {package} is outside repository root {root}") from error
    size = package.stat().st_size
    if not 1 <= size <= MAX_PACKAGE_BYTES:
        raise DistributionError(f"firmware package must be between 1 byte and {MAX_PACKAGE_BYTES} bytes")
    digest = hashlib.sha256(package.read_bytes()).hexdigest()
    return {
        "schema": "poison.release-manifest/v1",
        "version": version,
        "channel": channel,
        "target": "f7",
        "rollbackVersion": version,
        "components": [{
            "id": "firmware.update.tgz",
            "path": relative,
            "sha256": digest,
            "bytes": size,
        }],
        "revocations": [],
    }


def assemble(
    root: Path,
    package: Path,
    output: Path,
    version: str,
    channel: str,
    key_id: str,
    private_key: bytes,
    openssl: str = "openssl",
) -> dict:
    if not VERSION.fullmatch(version):
        raise DistributionError("release version must use MAJOR.MINOR.PATCH")
    if channel not in {"stable", "beta", "developer", "internal"}:
        raise DistributionError("release channel is invalid")
    if not KEY_ID.fullmatch(key_id):
        raise DistributionError("release key ID is invalid")
    package = package.resolve()
    root = root.resolve()
    output = output.resolve()
    if not package.is_file():
        raise DistributionError(f"firmware package does not exist: {package}")

    with tempfile.TemporaryDirectory(prefix="poison-web-installer-build-") as temporary:
        temporary_path = Path(temporary)
        private_path = temporary_path / "release-private.pem"
        public_path = temporary_path / "release-public.pem"
        manifest_path = temporary_path / "release.json"
        private_path.write_bytes(private_key)
        private_path.chmod(0o600)
        _validate_key(private_path, openssl)
        _public_key(private_path, openssl, public_path)
        manifest_path.write_text(
            json.dumps(_manifest(root, package, version, channel), indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        sign_release.sign(manifest_path, private_path, key_id, openssl)
        feed = feed_builder.build_feed(
            root,
            [(manifest_path, package, f"releases/{version}/{package.name}")],
            {key_id: public_path},
            openssl,
        )
        signed_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        release_dir = output / "releases" / version
        release_dir.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(package, release_dir / package.name)
        (release_dir / "release.json").write_text(
            json.dumps(signed_manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        (output / "release.json").write_text(
            json.dumps(signed_manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        (output / "releases.json").write_text(
            json.dumps(feed, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        (output / "installer-config.json").write_text(
            json.dumps({
                "schema": CONFIG_SCHEMA,
                "releaseFeedUrl": "./releases.json",
                "trustedReleaseKeys": {key_id: public_path.read_text(encoding="utf-8")},
            }, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    return signed_manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--channel", default="developer")
    parser.add_argument("--key-id", required=True)
    parser.add_argument("--private-key", type=Path)
    parser.add_argument("--private-key-base64-env", default="POISON_RELEASE_PRIVATE_KEY_B64")
    parser.add_argument("--openssl", default="openssl")
    arguments = parser.parse_args()
    try:
        private_key = arguments.private_key.read_bytes() if arguments.private_key else _private_key_from_environment(arguments.private_key_base64_env)
        manifest = assemble(
            arguments.root,
            arguments.package,
            arguments.output,
            arguments.version,
            arguments.channel,
            arguments.key_id,
            private_key,
            arguments.openssl,
        )
    except (DistributionError, OSError, json.JSONDecodeError, subprocess.SubprocessError) as error:
        parser.error(str(error))
    print(json.dumps({"version": manifest["version"], "channel": manifest["channel"], "keyId": manifest["signature"]["keyId"]}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
