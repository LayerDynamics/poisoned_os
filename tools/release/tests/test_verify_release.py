import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("verify_release", ROOT / "tools/release/verify_release.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class VerifyReleaseTests(unittest.TestCase):
    def test_component_digest_and_path_are_verified(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            component = root / "firmware.bin"
            component.write_bytes(b"firmware")
            manifest = root / "release.json"
            manifest.write_text(json.dumps({"schema": "poison.release-manifest/v1", "version": "1.0.0", "channel": "stable", "target": "f7", "rollbackVersion": "0.9.0", "components": [{"id": "firmware", "path": "firmware.bin", "sha256": hashlib.sha256(b"firmware").hexdigest(), "bytes": 8}], "revocations": []}), encoding="utf-8")
            self.assertEqual(MODULE.verify_manifest(manifest, root), [])
            component.write_bytes(b"tampered")
            self.assertTrue(any("mismatch" in item for item in MODULE.verify_manifest(manifest, root)))

    def test_release_metadata_rejects_invalid_revocations_and_signature(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            component = root / "firmware.bin"
            component.write_bytes(b"firmware")
            manifest = root / "release.json"
            manifest.write_text(json.dumps({
                "schema": "poison.release-manifest/v1",
                "version": "1.0.0",
                "channel": "stable",
                "target": "f7",
                "rollbackVersion": "0.9.0",
                "components": [{"id": "firmware", "path": "firmware.bin", "sha256": hashlib.sha256(b"firmware").hexdigest(), "bytes": 8}],
                "revocations": ["not-a-digest"],
                "signature": {"algorithm": "ECDSA-P256-SHA256", "keyId": "release-root", "value": "%%%"},
            }), encoding="utf-8")
            problems = MODULE.verify_manifest(manifest, root)
            self.assertIn("invalid revocations", problems)
            self.assertIn("invalid signature encoding", problems)
