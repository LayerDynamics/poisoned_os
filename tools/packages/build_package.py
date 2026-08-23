#!/usr/bin/env python3
"""Build deterministic, path-safe PoisonedOS content packages."""
from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import json
import re
import zipfile
from pathlib import Path


class PackageError(ValueError):
    pass


ALLOWED_TYPES = {"application", "firmware", "lesson", "tool-data", "theme", "font", "icon", "font-icon", "menu", "resource", "ui-pack"}
ARCHIVE_MAX_BYTES = 4 * 1024 * 1024
MANIFEST_MAX_BYTES = 4096
UINT32_MAX = 0xFFFFFFFF
IDENTIFIER = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")
SEMVER = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
FIRMWARE_API = re.compile(
    r"^(?:[0-9]+\.[0-9]+\.[0-9]+|>=[0-9]+\.[0-9]+\.[0-9]+ <[0-9]+\.[0-9]+\.[0-9]+)$"
)
REQUIRED_FIELDS = {
    "packageFormat", "contentType", "id", "version", "firmwareApi", "payloads",
    "entrypoint", "capabilities", "contentSha256", "signingKeyId", "releaseSequence",
}
PAYLOAD_FIELDS = {"path", "sha256", "size"}


def _digest(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and all(character in "0123456789abcdef" for character in value)
    )


def _semantic_version(value: object) -> bool:
    if not isinstance(value, str) or len(value) > 32 or not SEMVER.fullmatch(value):
        return False
    return all(int(part) <= UINT32_MAX for part in value.split("."))


def _firmware_api(value: object) -> bool:
    if not isinstance(value, str) or len(value) > 64 or not FIRMWARE_API.fullmatch(value):
        return False
    versions = re.findall(r"[0-9]+\.[0-9]+\.[0-9]+", value)
    return all(_semantic_version(version) for version in versions)


def _path(value: object) -> bool:
    if not isinstance(value, str) or not value or len(value) > 256 or value.startswith("/"):
        return False
    try:
        value.encode("ascii")
    except UnicodeEncodeError:
        return False
    if "\\" in value or any(ord(character) < 0x20 for character in value):
        return False
    return all(segment not in {"", ".", ".."} for segment in value.split("/"))


def _capability(value: object) -> bool:
    if not isinstance(value, str) or not 1 <= len(value) <= 64:
        return False
    exact = {
        "status", "device.status.read", "control", "launch", "device.app.run", "files",
        "evidence", "radio", "native", "destructive",
    }
    prefixes = (
        "device.read", "ui.", "runtime.", "notification.", "crypto.", "compute.", "app.",
        "storage.", "evidence.", "radio.", "nfc.", "lf-rfid.", "ibutton.", "infrared.",
        "sub-ghz.", "gpio.", "serial.", "ble.", "native.", "badusb.", "usb-hid.",
    )
    return value in exact or value.startswith(prefixes)


def canonical_json(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8")


def validate_manifest(manifest: dict) -> None:
    if not isinstance(manifest, dict):
        raise PackageError("manifest must be an object")
    missing = REQUIRED_FIELDS - manifest.keys()
    unexpected = manifest.keys() - REQUIRED_FIELDS - {"signature"}
    if missing or unexpected or manifest.get("packageFormat") != 1 or isinstance(manifest.get("packageFormat"), bool) or manifest.get("contentType") not in ALLOWED_TYPES:
        raise PackageError(f"invalid manifest fields: {sorted(missing)}")
    if not isinstance(manifest["id"], str) or not IDENTIFIER.fullmatch(manifest["id"]):
        raise PackageError("package id is invalid")
    if not _semantic_version(manifest["version"]):
        raise PackageError("version must be a bounded semantic version")
    if not _firmware_api(manifest["firmwareApi"]):
        raise PackageError("firmwareApi is invalid")
    if not isinstance(manifest["signingKeyId"], str) or not IDENTIFIER.fullmatch(manifest["signingKeyId"]):
        raise PackageError("signingKeyId is invalid")
    if not isinstance(manifest["payloads"], list) or not manifest["payloads"] or len(manifest["payloads"]) > 32:
        raise PackageError("payloads must contain 1..32 entries")
    paths: set[str] = set()
    payload_bytes = 0
    for payload in manifest["payloads"]:
        if not isinstance(payload, dict) or payload.keys() != PAYLOAD_FIELDS:
            raise PackageError("payload fields are invalid")
        path = payload.get("path", "")
        if not _path(path) or path in paths:
            raise PackageError("payload path is unsafe or duplicated")
        if not _digest(payload.get("sha256")):
            raise PackageError("payload digest must be lowercase SHA-256")
        if not isinstance(payload.get("size"), int) or isinstance(payload["size"], bool) or not 0 <= payload["size"] <= UINT32_MAX:
            raise PackageError("payload size is invalid")
        payload_bytes += payload["size"]
        paths.add(path)
    if manifest["entrypoint"] not in paths:
        raise PackageError("entrypoint must name a payload")
    if not isinstance(manifest["releaseSequence"], int) or isinstance(manifest["releaseSequence"], bool) or manifest["releaseSequence"] < 1 or manifest["releaseSequence"] > 0xFFFFFFFF:
        raise PackageError("releaseSequence must be a positive uint32")
    if not _digest(manifest.get("contentSha256")):
        raise PackageError("contentSha256 must be lowercase SHA-256")
    capabilities = manifest["capabilities"]
    if not isinstance(capabilities, list) or len(capabilities) > 32 or not all(_capability(capability) for capability in capabilities):
        raise PackageError("capabilities are invalid or duplicated")
    if len(set(capabilities)) != len(capabilities):
        raise PackageError("capabilities are invalid or duplicated")
    if "signature" in manifest:
        signature = manifest["signature"]
        if not isinstance(signature, str) or len(signature) > 512:
            raise PackageError("signature is invalid")
        try:
            decoded_signature = base64.b64decode(signature, validate=True)
        except (binascii.Error, ValueError) as error:
            raise PackageError("signature is invalid") from error
        if not 1 <= len(decoded_signature) <= 80:
            raise PackageError("signature is invalid")
    manifest_bytes = canonical_json(manifest)
    if len(manifest_bytes) > MANIFEST_MAX_BYTES:
        raise PackageError("manifest exceeds the device bound")
    member_names = ["manifest.json", *sorted(paths)]
    zip_bytes = payload_bytes + len(manifest_bytes) + 22
    zip_bytes += sum(76 + 2 * len(name.encode("ascii")) for name in member_names)
    if zip_bytes > ARCHIVE_MAX_BYTES:
        raise PackageError("package exceeds the device archive bound")


def content_digest(manifest: dict, payload_root: Path) -> str:
    digest = hashlib.sha256()
    for payload in sorted(manifest["payloads"], key=lambda item: item["path"]):
        data = (payload_root / payload["path"]).read_bytes()
        if len(data) != payload["size"] or hashlib.sha256(data).hexdigest() != payload["sha256"]:
            raise PackageError(f"payload digest/size mismatch: {payload['path']}")
        digest.update(payload["path"].encode("utf-8")); digest.update(b"\0"); digest.update(data)
    return digest.hexdigest()


def build(manifest_path: Path, payload_root: Path, output: Path) -> str:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8")); validate_manifest(manifest)
    if content_digest(manifest, payload_root) != manifest["contentSha256"]: raise PackageError("contentSha256 mismatch")
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_STORED) as archive:
        info = zipfile.ZipInfo("manifest.json", date_time=(1980, 1, 1, 0, 0, 0)); info.external_attr = 0o100644 << 16
        archive.writestr(info, canonical_json(manifest))
        for payload in sorted(manifest["payloads"], key=lambda item: item["path"]):
            info = zipfile.ZipInfo(payload["path"], date_time=(1980, 1, 1, 0, 0, 0)); info.external_attr = 0o100644 << 16
            archive.writestr(info, (payload_root / payload["path"]).read_bytes())
    if output.stat().st_size > ARCHIVE_MAX_BYTES:
        raise PackageError("package exceeds the device archive bound")
    return hashlib.sha256(output.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(); parser.add_argument("--manifest", type=Path, required=True); parser.add_argument("--payload-root", type=Path, required=True); parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try: print(build(args.manifest, args.payload_root, args.output)); return 0
    except (PackageError, OSError, json.JSONDecodeError) as error: parser.error(str(error)); return 2


if __name__ == "__main__": raise SystemExit(main())
