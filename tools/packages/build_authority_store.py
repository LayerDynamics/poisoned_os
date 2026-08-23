#!/usr/bin/env python3
"""Build the bounded device trust-store format used by signed content packages."""

from __future__ import annotations

import argparse
import base64
from dataclasses import dataclass
from pathlib import Path
import re
import subprocess


KEY_ID = re.compile(r"[a-z0-9][a-z0-9._-]{0,63}\Z")
MAX_AUTHORITIES = 8
PUBLIC_KEY_BYTES = 65


class AuthorityStoreError(ValueError):
    pass


@dataclass(frozen=True)
class Authority:
    key_id: str
    public_key: bytes
    revoked: bool = False


def parse_public_key(path: Path) -> bytes:
    try:
        result = subprocess.run(
            ["openssl", "pkey", "-pubin", "-in", str(path), "-outform", "DER"],
            capture_output=True,
            check=False,
        )
    except OSError as error:
        raise AuthorityStoreError(f"cannot execute OpenSSL: {error}") from error
    if result.returncode != 0:
        raise AuthorityStoreError(
            result.stderr.decode("utf-8", errors="replace").strip()
            or f"invalid public key: {path}"
        )
    prefix = bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d030107034200")
    if not result.stdout.startswith(prefix) or len(result.stdout) != len(prefix) + PUBLIC_KEY_BYTES:
        raise AuthorityStoreError("public key must be an uncompressed P-256 SubjectPublicKeyInfo")
    public_key = result.stdout[len(prefix):]
    if public_key[0] != 0x04:
        raise AuthorityStoreError("public key must use uncompressed P-256 encoding")
    return public_key


def parse_spec(value: str, revoked: bool) -> Authority:
    key_id, separator, path = value.partition("=")
    if not separator or KEY_ID.fullmatch(key_id) is None or not path:
        raise AuthorityStoreError("authority must use KEY_ID=PUBLIC_KEY.pem")
    return Authority(key_id, parse_public_key(Path(path)), revoked)


def encode(authorities: list[Authority]) -> bytes:
    if not authorities or len(authorities) > MAX_AUTHORITIES:
        raise AuthorityStoreError("authority store must contain 1..8 keys")
    seen: set[str] = set()
    output = bytearray(b"PPK1\x01")
    output.extend((len(authorities), 0, 0))
    for authority in sorted(authorities, key=lambda item: item.key_id):
        if KEY_ID.fullmatch(authority.key_id) is None or authority.key_id in seen:
            raise AuthorityStoreError(f"duplicate or invalid key ID: {authority.key_id}")
        if len(authority.public_key) != PUBLIC_KEY_BYTES or authority.public_key[0] != 0x04:
            raise AuthorityStoreError(f"invalid P-256 public key: {authority.key_id}")
        seen.add(authority.key_id)
        encoded_id = authority.key_id.encode("ascii")
        output.extend((len(encoded_id), 1 if authority.revoked else 0, 0, 0))
        output.extend(authority.public_key)
        output.extend(encoded_id)
    if len(output) > 1200:
        raise AuthorityStoreError("encoded authority store exceeds firmware bound")
    return bytes(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--key", action="append", default=[], metavar="ID=PEM")
    parser.add_argument("--revoked-key", action="append", default=[], metavar="ID=PEM")
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        authorities = [parse_spec(value, False) for value in arguments.key]
        authorities.extend(parse_spec(value, True) for value in arguments.revoked_key)
        encoded = encode(authorities)
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_bytes(encoded)
        return 0
    except (AuthorityStoreError, OSError) as error:
        parser.error(str(error))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
