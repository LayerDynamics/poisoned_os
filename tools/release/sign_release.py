#!/usr/bin/env python3
"""Sign a release manifest with the approved OpenSSL ECDSA primitive."""

from __future__ import annotations

import argparse
import base64
import json
from pathlib import Path
import subprocess
import tempfile

ALGORITHM = "ECDSA-P256-SHA256"


def canonical_payload(manifest: dict) -> bytes:
    unsigned = {key: value for key, value in manifest.items() if key != "signature"}
    return (json.dumps(unsigned, ensure_ascii=False, separators=(",", ":"), sort_keys=True) + "\n").encode()


def sign(manifest_path: Path, private_key: Path, key_id: str, openssl: str = "openssl") -> None:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or manifest.get("schema") != "poison.release-manifest/v1": raise ValueError("invalid release manifest")
    if not key_id or len(key_id) > 64: raise ValueError("invalid key id")
    with tempfile.TemporaryDirectory(prefix="poison-release-sign-") as directory:
        payload = Path(directory) / "payload.json"; signature = Path(directory) / "signature.der"
        payload.write_bytes(canonical_payload(manifest))
        result = subprocess.run([openssl, "dgst", "-sha256", "-sign", str(private_key), "-out", str(signature), str(payload)], capture_output=True, text=True, check=False)
        if result.returncode != 0: raise ValueError(result.stderr.strip() or "OpenSSL signing failed")
        manifest["signature"] = {"algorithm": ALGORITHM, "keyId": key_id, "value": base64.b64encode(signature.read_bytes()).decode("ascii")}
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(); parser.add_argument("manifest", type=Path); parser.add_argument("--private-key", type=Path, required=True); parser.add_argument("--key-id", required=True); parser.add_argument("--openssl", default="openssl")
    args = parser.parse_args()
    try: sign(args.manifest, args.private_key, args.key_id, args.openssl); return 0
    except (OSError, ValueError, json.JSONDecodeError) as error: parser.error(str(error)); return 2


if __name__ == "__main__": raise SystemExit(main())
