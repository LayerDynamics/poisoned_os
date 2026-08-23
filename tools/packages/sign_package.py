#!/usr/bin/env python3
"""Sign a canonical package manifest with the M0-approved OpenSSL primitive."""
from __future__ import annotations

import argparse
import base64
import json
import subprocess
import tempfile
from pathlib import Path

from build_package import canonical_json, validate_manifest


def sign(manifest_path: Path, private_key: Path, output: Path, openssl: str = "openssl") -> None:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8")); validate_manifest(manifest)
    manifest.pop("signature", None)
    with tempfile.TemporaryDirectory(prefix="poison-package-sign-") as directory:
        payload = Path(directory) / "manifest.json"; signature = Path(directory) / "signature.der"
        payload.write_bytes(canonical_json(manifest))
        subprocess.run([openssl, "dgst", "-sha256", "-sign", str(private_key), "-out", str(signature), str(payload)], check=True)
        manifest["signature"] = base64.b64encode(signature.read_bytes()).decode("ascii")
    validate_manifest(manifest)
    output.write_bytes(canonical_json(manifest))


def main() -> int:
    parser = argparse.ArgumentParser(); parser.add_argument("--manifest", type=Path, required=True); parser.add_argument("--private-key", type=Path, required=True); parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(); sign(args.manifest, args.private_key, args.output); return 0


if __name__ == "__main__": raise SystemExit(main())
