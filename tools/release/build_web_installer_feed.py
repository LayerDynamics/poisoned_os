#!/usr/bin/env python3
"""Build a browser-installer feed from cryptographically verified release manifests."""

from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
from urllib.parse import urlparse


FEED_SCHEMA = "poison.web-installer-feed/v1"
RELEASE_SCHEMA = "poison.release-manifest/v1"
ALGORITHM = "ECDSA-P256-SHA256"
DIGEST = re.compile(r"^[0-9a-f]{64}$")
IDENTIFIER = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")
VERSION = re.compile(r"^\d+\.\d+\.\d+$")
COMPONENT_PATH = re.compile(r"^(?:[a-zA-Z0-9._-]+/)*[a-zA-Z0-9._-]+$")
MAX_PACKAGE_BYTES = 32 * 1024 * 1024
MAX_COMPONENT_BYTES = (1 << 53) - 1
MANIFEST_FIELDS = {
    "schema",
    "version",
    "channel",
    "target",
    "rollbackVersion",
    "minimumVersion",
    "maximumVersion",
    "components",
    "revocations",
    "signature",
}
REQUIRED_MANIFEST_FIELDS = MANIFEST_FIELDS - {"minimumVersion", "maximumVersion"}


class FeedError(ValueError):
    """A release cannot safely be admitted to the browser installer feed."""


def canonical_payload(manifest: dict) -> bytes:
    unsigned = {key: value for key, value in manifest.items() if key != "signature"}
    return (
        json.dumps(unsigned, ensure_ascii=False, separators=(",", ":"), sort_keys=True)
        + "\n"
    ).encode("utf-8")


def _safe_package_url(value: str) -> None:
    if not value or len(value) > 2048 or "\\" in value:
        raise FeedError("package URL is invalid")
    parsed = urlparse(value)
    if parsed.scheme:
        if parsed.scheme != "https" or not parsed.netloc or parsed.username or parsed.password:
            raise FeedError("absolute package URLs must use HTTPS without credentials")
    elif value.startswith("/") or any(part in {"", ".", ".."} for part in value.split("/")):
        raise FeedError("relative package URL must be a safe path")


def _load_manifest(path: Path) -> dict:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise FeedError(f"cannot read release manifest {path}: {error}") from error
    if not isinstance(manifest, dict) or manifest.get("schema") != RELEASE_SCHEMA:
        raise FeedError(f"{path} is not a PoisonedOS release manifest")
    fields = set(manifest)
    if not REQUIRED_MANIFEST_FIELDS.issubset(fields) or not fields.issubset(MANIFEST_FIELDS):
        raise FeedError(f"{path} has invalid release manifest fields")
    for name in ("version", "rollbackVersion"):
        if not isinstance(manifest.get(name), str) or not VERSION.fullmatch(manifest[name]):
            raise FeedError(f"{path} has an invalid {name}")
    for name in ("minimumVersion", "maximumVersion"):
        if name in manifest and (
            not isinstance(manifest[name], str) or not VERSION.fullmatch(manifest[name])
        ):
            raise FeedError(f"{path} has an invalid {name}")
    if manifest.get("target") not in {"7", "f7"}:
        raise FeedError(f"{path} is not for Flipper Zero target 7")
    if manifest.get("channel") not in {"internal", "developer", "beta", "stable"}:
        raise FeedError(f"{path} has an invalid release channel")
    signature = manifest.get("signature")
    if (
        not isinstance(signature, dict)
        or set(signature) != {"algorithm", "keyId", "value"}
        or signature.get("algorithm") != ALGORITHM
        or not isinstance(signature.get("keyId"), str)
        or not IDENTIFIER.fullmatch(signature["keyId"])
        or not isinstance(signature.get("value"), str)
    ):
        raise FeedError(f"{path} is not signed with approved release metadata")
    components = manifest.get("components")
    if not isinstance(components, list) or not 1 <= len(components) <= 64:
        raise FeedError(f"{path} must contain 1 to 64 release components")
    component_ids: set[str] = set()
    for component in components:
        if not isinstance(component, dict) or set(component) != {"id", "path", "sha256", "bytes"}:
            raise FeedError(f"{path} has an invalid release component")
        identifier = component.get("id")
        component_path = component.get("path")
        digest = component.get("sha256")
        size = component.get("bytes")
        if not isinstance(identifier, str) or not IDENTIFIER.fullmatch(identifier) or identifier in component_ids:
            raise FeedError(f"{path} has an invalid or duplicate release component id")
        component_ids.add(identifier)
        if (
            not isinstance(component_path, str)
            or not COMPONENT_PATH.fullmatch(component_path)
        ):
            raise FeedError(f"{path} has an unsafe release component path")
        if not isinstance(digest, str) or not DIGEST.fullmatch(digest):
            raise FeedError(f"{path} has an invalid release component digest")
        if not isinstance(size, int) or isinstance(size, bool) or not 1 <= size <= MAX_COMPONENT_BYTES:
            raise FeedError(f"{path} has an invalid release component size")
    revocations = manifest.get("revocations")
    if (
        not isinstance(revocations, list)
        or len(revocations) > 64
        or any(not isinstance(value, str) or not DIGEST.fullmatch(value) for value in revocations)
        or len(revocations) != len(set(revocations))
    ):
        raise FeedError(f"{path} has invalid release revocations")
    return manifest


def _verify_signature(manifest: dict, public_key: Path, openssl: str) -> None:
    try:
        signature = base64.b64decode(manifest["signature"]["value"], validate=True)
    except (ValueError, binascii.Error) as error:
        raise FeedError("release signature is not valid base64") from error
    with tempfile.TemporaryDirectory(prefix="poison-web-feed-") as directory:
        temporary = Path(directory)
        payload_path = temporary / "payload.json"
        signature_path = temporary / "signature.der"
        payload_path.write_bytes(canonical_payload(manifest))
        signature_path.write_bytes(signature)
        result = subprocess.run(
            [
                openssl,
                "dgst",
                "-sha256",
                "-verify",
                str(public_key),
                "-signature",
                str(signature_path),
                str(payload_path),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
    if result.returncode != 0 or "Verified OK" not in result.stdout:
        detail = result.stderr.strip() or result.stdout.strip() or "OpenSSL verification failed"
        raise FeedError(f"release signature is invalid: {detail}")


def _select_component(manifest: dict, package: Path, root: Path) -> dict:
    try:
        relative = package.resolve().relative_to(root.resolve()).as_posix()
    except ValueError as error:
        raise FeedError(f"package {package} is outside release root {root}") from error
    matches = [
        component
        for component in manifest["components"]
        if isinstance(component, dict) and component.get("path") == relative
    ]
    if len(matches) != 1:
        raise FeedError(f"signed release manifest does not contain exactly one {relative} component")
    selected = matches[0]
    if not isinstance(selected.get("id"), str) or not IDENTIFIER.fullmatch(selected["id"]):
        raise FeedError("installer package component id is invalid")
    if not relative.endswith(".tgz"):
        raise FeedError("installer package component must be a .tgz archive")
    try:
        package_bytes = package.stat().st_size
    except OSError as error:
        raise FeedError(f"cannot inspect installer package {package}: {error}") from error
    if not 1 <= package_bytes <= MAX_PACKAGE_BYTES:
        raise FeedError("installer package has an invalid size")
    try:
        with package.open("rb") as source:
            digest = hashlib.file_digest(source, "sha256").hexdigest()
    except OSError as error:
        raise FeedError(f"cannot read installer package {package}: {error}") from error
    if selected.get("bytes") != package_bytes or selected.get("sha256") != digest:
        raise FeedError("installer package does not match its signed release component")
    if not DIGEST.fullmatch(digest):
        raise FeedError("installer package digest is invalid")
    return selected


def build_feed(
    root: Path,
    releases: list[tuple[Path, Path, str]],
    public_keys: dict[str, Path],
    openssl: str = "openssl",
) -> dict:
    if not releases:
        raise FeedError("at least one release is required")
    if len(releases) > 32:
        raise FeedError("installer feed cannot contain more than 32 releases")
    entries = []
    identities: set[tuple[str, str]] = set()
    for manifest_path, package_path, package_url in releases:
        _safe_package_url(package_url)
        manifest = _load_manifest(manifest_path)
        key_id = manifest["signature"]["keyId"]
        public_key = public_keys.get(key_id)
        if public_key is None:
            raise FeedError(f"no trusted public key was supplied for {key_id}")
        if not public_key.is_file():
            raise FeedError(f"trusted public key does not exist: {public_key}")
        _verify_signature(manifest, public_key, openssl)
        selected = _select_component(manifest, package_path, root)
        identity = (manifest["channel"], manifest["version"])
        if identity in identities:
            raise FeedError(f"duplicate {identity[0]} {identity[1]} release")
        identities.add(identity)
        entries.append(
            {
                "manifest": manifest,
                "packageComponentId": selected["id"],
                "packageUrl": package_url,
            }
        )
    entries.sort(key=lambda entry: (entry["manifest"]["channel"], entry["manifest"]["version"]))
    return {"schema": FEED_SCHEMA, "releases": entries}


def _public_key(value: str) -> tuple[str, Path]:
    key_id, separator, path = value.partition("=")
    if not separator or not IDENTIFIER.fullmatch(key_id) or not path:
        raise argparse.ArgumentTypeError("public keys must use KEY_ID=/path/to/public.pem")
    return key_id, Path(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--public-key", action="append", type=_public_key, required=True)
    parser.add_argument(
        "--release",
        action="append",
        nargs=3,
        metavar=("MANIFEST", "PACKAGE", "PACKAGE_URL"),
        required=True,
    )
    parser.add_argument("--openssl", default="openssl")
    args = parser.parse_args()
    try:
        releases = [(Path(item[0]), Path(item[1]), item[2]) for item in args.release]
        feed = build_feed(args.root, releases, dict(args.public_key), args.openssl)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(feed, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        return 0
    except (FeedError, OSError) as error:
        parser.error(str(error))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
