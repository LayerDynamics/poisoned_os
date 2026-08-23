from __future__ import annotations

import base64
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
VERIFIER = REPOSITORY_ROOT / "tools" / "signing" / "verify_manifest.py"
OPENSSL = REPOSITORY_ROOT / "toolchain" / "arm64-darwin" / "bin" / "openssl"
ARTIFACT_DIGEST = "a" * 64
SIGNER_KEY_ID = "package-intermediate-test"


def canonical_payload(manifest: dict[str, object]) -> bytes:
    payload = {key: value for key, value in manifest.items() if key != "signature"}
    return json.dumps(
        payload, ensure_ascii=False, separators=(",", ":"), sort_keys=True
    ).encode("utf-8")


class ManifestFixture:
    def __init__(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.private_key = self.root / "test-private.pem"
        self.public_key = self.root / "test-public.pem"
        self.manifest_path = self.root / "revocations.json"
        subprocess.run(
            [
                os.fspath(OPENSSL),
                "genpkey",
                "-algorithm",
                "EC",
                "-pkeyopt",
                "ec_paramgen_curve:P-256",
                "-out",
                os.fspath(self.private_key),
            ],
            check=True,
            capture_output=True,
        )
        subprocess.run(
            [
                os.fspath(OPENSSL),
                "pkey",
                "-in",
                os.fspath(self.private_key),
                "-pubout",
                "-out",
                os.fspath(self.public_key),
            ],
            check=True,
            capture_output=True,
        )
        self.manifest: dict[str, object] = {
            "schema": "poison.revocations/v1",
            "version": 1,
            "sequence": 5,
            "issuedAt": "2026-08-21T00:00:00Z",
            "expiresAt": "2026-08-22T00:00:00Z",
            "scope": "package",
            "issuerKeyId": "revocation-test-root",
            "revokedKeyIds": [],
            "revokedArtifactDigests": [],
            "signatureAlgorithm": "ECDSA-P256-SHA256",
        }
        self.sign()

    def sign(self) -> None:
        payload_path = self.root / "payload.json"
        signature_path = self.root / "signature.der"
        payload_path.write_bytes(canonical_payload(self.manifest))
        subprocess.run(
            [
                os.fspath(OPENSSL),
                "dgst",
                "-sha256",
                "-sign",
                os.fspath(self.private_key),
                "-out",
                os.fspath(signature_path),
                os.fspath(payload_path),
            ],
            check=True,
            capture_output=True,
        )
        self.manifest["signature"] = base64.b64encode(signature_path.read_bytes()).decode(
            "ascii"
        )
        self.manifest_path.write_text(
            json.dumps(self.manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    def run(self, *extra: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                os.fspath(VERIFIER),
                "--manifest",
                os.fspath(self.manifest_path),
                "--public-key",
                os.fspath(self.public_key),
                "--openssl",
                os.fspath(OPENSSL),
                "--scope",
                "package",
                "--minimum-sequence",
                "4",
                "--at-time",
                "2026-08-21T12:00:00Z",
                "--signer-key-id",
                SIGNER_KEY_ID,
                "--artifact-digest",
                ARTIFACT_DIGEST,
                *extra,
            ],
            capture_output=True,
            check=False,
            text=True,
        )

    def close(self) -> None:
        self.temporary.cleanup()


class VerifyManifestTests(unittest.TestCase):
    def setUp(self) -> None:
        self.fixture = ManifestFixture()

    def tearDown(self) -> None:
        self.fixture.close()

    def test_accepts_valid_signed_revocation_manifest(self) -> None:
        result = self.fixture.run()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "revocation manifest verification passed: package sequence 5\n",
        )

    def test_rejects_tampered_manifest(self) -> None:
        self.fixture.manifest["sequence"] = 6
        self.fixture.manifest_path.write_text(
            json.dumps(self.fixture.manifest), encoding="utf-8"
        )
        result = self.fixture.run()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("signature verification failed", result.stderr)

    def test_rejects_expired_manifest(self) -> None:
        result = self.fixture.run("--at-time", "2026-08-23T00:00:00Z")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("manifest expired", result.stderr)

    def test_rejects_wrong_scope(self) -> None:
        result = self.fixture.run("--scope", "firmware")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("scope mismatch", result.stderr)

    def test_rejects_downgraded_sequence(self) -> None:
        result = self.fixture.run("--minimum-sequence", "6")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("rollback rejected", result.stderr)

    def test_rejects_revoked_signer(self) -> None:
        self.fixture.manifest["revokedKeyIds"] = [SIGNER_KEY_ID]
        self.fixture.sign()
        result = self.fixture.run()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("signer key is revoked", result.stderr)

    def test_rejects_revoked_artifact(self) -> None:
        self.fixture.manifest["revokedArtifactDigests"] = [ARTIFACT_DIGEST]
        self.fixture.sign()
        result = self.fixture.run()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("artifact digest is revoked", result.stderr)


if __name__ == "__main__":
    unittest.main()
