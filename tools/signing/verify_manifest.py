#!/usr/bin/env python3
"""Verify a signed PoisonedOS revocation manifest and rollback/revocation policy."""

from __future__ import annotations

import argparse
import base64
import binascii
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Any


SCHEMA = "poison.revocations/v1"
ALGORITHM = "ECDSA-P256-SHA256"
SCOPES = {"content", "firmware", "package", "provenance", "support-bundle"}
REQUIRED_FIELDS = {
    "schema",
    "version",
    "sequence",
    "issuedAt",
    "expiresAt",
    "scope",
    "issuerKeyId",
    "revokedKeyIds",
    "revokedArtifactDigests",
    "signatureAlgorithm",
    "signature",
}
KEY_ID_PATTERN = re.compile(r"[a-z0-9][a-z0-9._-]{0,63}\Z")
DIGEST_PATTERN = re.compile(r"[0-9a-f]{64}\Z")


class ManifestError(ValueError):
    """Raised when signature or revocation policy validation fails."""


def parse_timestamp(value: Any, field: str) -> datetime:
    if not isinstance(value, str):
        raise ManifestError(f"{field} must be an RFC 3339 timestamp")
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as error:
        raise ManifestError(f"{field} must be an RFC 3339 timestamp") from error
    if parsed.tzinfo is None:
        raise ManifestError(f"{field} must include a UTC offset")
    return parsed.astimezone(timezone.utc)


def validate_string_list(value: Any, field: str, pattern: re.Pattern[str]) -> list[str]:
    if (
        not isinstance(value, list)
        or not all(isinstance(item, str) and pattern.fullmatch(item) for item in value)
        or len(set(value)) != len(value)
    ):
        raise ManifestError(f"{field} must contain unique valid values")
    return value


def load_and_validate(path: Path) -> dict[str, Any]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ManifestError(f"cannot load manifest: {error}") from error
    if not isinstance(manifest, dict):
        raise ManifestError("manifest must be a JSON object")
    missing = REQUIRED_FIELDS - set(manifest)
    extra = set(manifest) - REQUIRED_FIELDS
    if missing:
        raise ManifestError(f"manifest missing required field: {sorted(missing)[0]}")
    if extra:
        raise ManifestError(f"manifest has unknown field: {sorted(extra)[0]}")
    if manifest["schema"] != SCHEMA:
        raise ManifestError(f"schema must equal {SCHEMA}")
    if manifest["version"] != 1 or isinstance(manifest["version"], bool):
        raise ManifestError("version must equal 1")
    if (
        not isinstance(manifest["sequence"], int)
        or isinstance(manifest["sequence"], bool)
        or manifest["sequence"] < 0
    ):
        raise ManifestError("sequence must be a non-negative integer")
    if manifest["scope"] not in SCOPES:
        raise ManifestError("scope is invalid")
    if not isinstance(manifest["issuerKeyId"], str) or KEY_ID_PATTERN.fullmatch(
        manifest["issuerKeyId"]
    ) is None:
        raise ManifestError("issuerKeyId is invalid")
    validate_string_list(manifest["revokedKeyIds"], "revokedKeyIds", KEY_ID_PATTERN)
    validate_string_list(
        manifest["revokedArtifactDigests"], "revokedArtifactDigests", DIGEST_PATTERN
    )
    if manifest["signatureAlgorithm"] != ALGORITHM:
        raise ManifestError(f"signatureAlgorithm must equal {ALGORITHM}")
    if not isinstance(manifest["signature"], str):
        raise ManifestError("signature must be base64 text")
    issued = parse_timestamp(manifest["issuedAt"], "issuedAt")
    expires = parse_timestamp(manifest["expiresAt"], "expiresAt")
    if expires <= issued:
        raise ManifestError("expiresAt must be later than issuedAt")
    return manifest


def canonical_payload(manifest: dict[str, Any]) -> bytes:
    payload = {key: value for key, value in manifest.items() if key != "signature"}
    return json.dumps(
        payload, ensure_ascii=False, separators=(",", ":"), sort_keys=True
    ).encode("utf-8")


def verify_signature(
    manifest: dict[str, Any], public_key: Path, openssl: Path
) -> None:
    if not public_key.is_file():
        raise ManifestError(f"public key does not exist: {public_key}")
    if not openssl.is_file() or not os.access(openssl, os.X_OK):
        raise ManifestError(f"OpenSSL executable does not exist: {openssl}")
    key_result = subprocess.run(
        [os.fspath(openssl), "pkey", "-pubin", "-in", os.fspath(public_key), "-text", "-noout"],
        capture_output=True,
        check=False,
        text=True,
    )
    key_text = key_result.stdout + key_result.stderr
    if key_result.returncode != 0:
        raise ManifestError("public key could not be parsed")
    if "ASN1 OID: prime256v1" not in key_text and "NIST CURVE: P-256" not in key_text:
        raise ManifestError("public key must use P-256")
    try:
        signature = base64.b64decode(manifest["signature"], validate=True)
    except (ValueError, binascii.Error) as error:
        raise ManifestError("signature must be valid base64") from error
    with tempfile.TemporaryDirectory(prefix="poison-signature-verify-") as directory:
        temporary = Path(directory)
        payload_path = temporary / "payload.json"
        signature_path = temporary / "signature.der"
        payload_path.write_bytes(canonical_payload(manifest))
        signature_path.write_bytes(signature)
        result = subprocess.run(
            [
                os.fspath(openssl),
                "dgst",
                "-sha256",
                "-verify",
                os.fspath(public_key),
                "-signature",
                os.fspath(signature_path),
                os.fspath(payload_path),
            ],
            capture_output=True,
            check=False,
            text=True,
        )
    if result.returncode != 0 or result.stdout.strip() != "Verified OK":
        raise ManifestError("signature verification failed")


def verify_policy(
    manifest: dict[str, Any],
    expected_scope: str,
    minimum_sequence: int,
    at_time: datetime,
    signer_key_id: str | None,
    artifact_digest: str | None,
) -> None:
    if expected_scope not in SCOPES:
        raise ManifestError("requested scope is invalid")
    if manifest["scope"] != expected_scope:
        raise ManifestError(
            f"scope mismatch: expected {expected_scope}, found {manifest['scope']}"
        )
    if manifest["sequence"] < minimum_sequence:
        raise ManifestError(
            f"rollback rejected: sequence {manifest['sequence']} is below {minimum_sequence}"
        )
    issued = parse_timestamp(manifest["issuedAt"], "issuedAt")
    expires = parse_timestamp(manifest["expiresAt"], "expiresAt")
    if at_time < issued:
        raise ManifestError("manifest is not yet valid")
    if at_time >= expires:
        raise ManifestError("manifest expired")
    if signer_key_id is not None:
        if KEY_ID_PATTERN.fullmatch(signer_key_id) is None:
            raise ManifestError("signer key ID is invalid")
        if signer_key_id in manifest["revokedKeyIds"]:
            raise ManifestError("signer key is revoked")
    if artifact_digest is not None:
        if DIGEST_PATTERN.fullmatch(artifact_digest) is None:
            raise ManifestError("artifact digest is invalid")
        if artifact_digest in manifest["revokedArtifactDigests"]:
            raise ManifestError("artifact digest is revoked")


def default_openssl() -> Path:
    repository = Path(__file__).resolve().parents[2]
    bundled = repository / "toolchain" / "current" / "bin" / "openssl"
    if bundled.is_file():
        return bundled
    discovered = shutil.which("openssl")
    return Path(discovered) if discovered else bundled


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--public-key", type=Path, required=True)
    parser.add_argument("--openssl", type=Path, default=default_openssl())
    parser.add_argument("--scope", choices=sorted(SCOPES), required=True)
    parser.add_argument("--minimum-sequence", type=int, required=True)
    parser.add_argument("--at-time", required=True)
    parser.add_argument("--signer-key-id")
    parser.add_argument("--artifact-digest")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        if arguments.minimum_sequence < 0:
            raise ManifestError("minimum sequence must be non-negative")
        at_time = parse_timestamp(arguments.at_time, "at-time")
        manifest = load_and_validate(arguments.manifest.resolve())
        verify_signature(manifest, arguments.public_key.resolve(), arguments.openssl.resolve())
        verify_policy(
            manifest,
            arguments.scope,
            arguments.minimum_sequence,
            at_time,
            arguments.signer_key_id,
            arguments.artifact_digest,
        )
    except (ManifestError, OSError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        return 1
    print(
        f"revocation manifest verification passed: {manifest['scope']} "
        f"sequence {manifest['sequence']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
