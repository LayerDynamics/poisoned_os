import importlib.util
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("verify_migration_manifest", ROOT / "tools/migration/verify_manifest.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class VerifyMigrationManifestTests(unittest.TestCase):
    def test_accepts_matching_source_and_verified_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_root, backup_root = root / "source", root / "backup"
            (source_root / "ext").mkdir(parents=True)
            (backup_root / "legacy").mkdir(parents=True)
            source = b"legacy capture"
            backup = b"verified backup"
            (source_root / "ext/capture.sub").write_bytes(source)
            (backup_root / "legacy/capture.sub").write_bytes(backup)
            manifest = {"schema": "poison.migration-manifest/v1", "converterVersion": "1.0.0", "entries": [{"sourcePath": "/ext/capture.sub", "sourceSha256": hashlib.sha256(source).hexdigest(), "bytes": len(source), "logicalPath": "/captures/capture.sub", "classification": "convertible", "requiredFreeBytes": 64, "backup": {"path": "legacy/capture.sub", "sha256": hashlib.sha256(backup).hexdigest(), "bytes": len(backup), "verified": True}}]}
            manifest_path = root / "manifest.json"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            self.assertEqual(MODULE.verify_manifest(manifest_path, source_root, backup_root), [])

    def test_rejects_traversal_and_tampered_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_root, backup_root = root / "source", root / "backup"
            source_root.mkdir(); backup_root.mkdir()
            manifest = {"schema": "poison.migration-manifest/v1", "converterVersion": "1.0.0", "entries": [{"sourcePath": "/ext/../secret", "sourceSha256": "0" * 64, "bytes": 1, "logicalPath": "/unknown/x", "classification": "unknown", "requiredFreeBytes": 0, "backup": {"path": "missing", "sha256": "0" * 64, "bytes": 1, "verified": True}}]}
            path = root / "manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            self.assertTrue(MODULE.verify_manifest(path, source_root, backup_root))
