from __future__ import annotations

import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def load():
    spec = importlib.util.spec_from_file_location("build_package", ROOT / "tools/packages/build_package.py")
    module = importlib.util.module_from_spec(spec); assert spec.loader is not None; spec.loader.exec_module(module); return module


class PackageReproducibilityTests(unittest.TestCase):
    def manifest(self, **changes):
        manifest = {
            "packageFormat": 1,
            "contentType": "application",
            "id": "org.example.safe",
            "version": "1.0.0",
            "firmwareApi": ">=1.0.0 <2.0.0",
            "payloads": [{"path": "app.fap", "sha256": "0" * 64, "size": 8}],
            "entrypoint": "app.fap",
            "capabilities": ["storage.project.read"],
            "contentSha256": "0" * 64,
            "signingKeyId": "package-test-1",
            "releaseSequence": 1,
        }
        manifest.update(changes)
        return manifest

    def test_same_manifest_and_payloads_produce_same_digest(self):
        module = load()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); payload = root / "app.fap"; payload.write_bytes(b"safe app")
            payload_sha = hashlib.sha256(payload.read_bytes()).hexdigest()
            content_sha = hashlib.sha256(b"app.fap\0safe app").hexdigest()
            manifest = {"packageFormat": 1, "contentType": "application", "id": "org.example.safe", "version": "1.0.0", "firmwareApi": ">=1.0.0 <2.0.0", "payloads": [{"path": "app.fap", "sha256": payload_sha, "size": payload.stat().st_size}], "entrypoint": "app.fap", "capabilities": [], "contentSha256": content_sha, "signingKeyId": "test", "releaseSequence": 1}
            manifest_path = root / "manifest.json"; manifest_path.write_bytes(module.canonical_json(manifest))
            first = root / "first.zip"; second = root / "second.zip"; module.build(manifest_path, root, first); module.build(manifest_path, root, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_rejects_path_traversal(self):
        module = load()
        with self.assertRaises(module.PackageError):
            module.validate_manifest(self.manifest(payloads=[{
                "path": "../bad", "sha256": "0" * 64, "size": 1,
            }], entrypoint="../bad"))

    def test_rejects_contract_fields_the_device_cannot_accept(self):
        module = load()
        invalid = [
            self.manifest(id="Org.Example"),
            self.manifest(id="a" * 65),
            self.manifest(version="1.0"),
            self.manifest(firmwareApi=">=1.0 <2.0"),
            self.manifest(signingKeyId="key with spaces"),
            self.manifest(capabilities=["storage.project.read", "storage.project.read"]),
            self.manifest(capabilities=["host.shell"]),
            self.manifest(capabilities=[{"name": "storage.project.read"}]),
            self.manifest(unexpected=True),
        ]
        for manifest in invalid:
            with self.subTest(manifest=manifest):
                with self.assertRaises(module.PackageError):
                    module.validate_manifest(manifest)

    def test_rejects_unsupported_content_type_and_noncanonical_payload(self):
        module = load()
        invalid = [
            self.manifest(contentType="plugin"),
            self.manifest(payloads=[{
                "path": "app.fap", "sha256": "0" * 64, "size": 8, "mode": "755",
            }]),
            self.manifest(payloads=[{
                "path": "app.fap\nignored", "sha256": "0" * 64, "size": 8,
            }], entrypoint="app.fap\nignored"),
            self.manifest(payloads=[
                {"path": "app.fap", "sha256": "0" * 64, "size": 8},
                {"path": "app.fap", "sha256": "1" * 64, "size": 1},
            ]),
        ]
        for manifest in invalid:
            with self.subTest(manifest=manifest):
                with self.assertRaises(module.PackageError):
                    module.validate_manifest(manifest)


if __name__ == "__main__": unittest.main()
