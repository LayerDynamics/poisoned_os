import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("verify_redaction", ROOT / "tools/security/verify_redaction.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class VerifyRedactionTests(unittest.TestCase):
    def write(self, value: str, suffix: str = ".json") -> Path:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / f"bundle{suffix}"
        path.write_text(value, encoding="utf-8")
        return path

    def test_rejects_sensitive_fields_and_pem(self):
        path = self.write(json.dumps({"events": [{"summary": "-----BEGIN PRIVATE KEY-----"}], "accessToken": "redacted"}))
        failures = MODULE.scan(path)
        self.assertGreaterEqual(len(failures), 2)

    def test_accepts_digest_only_bundle(self):
        path = self.write(json.dumps({"schema": "poison.support-bundle/v1", "files": [{"path": "/ext/case.bin", "sha256": "a" * 64}], "summary": "transport timeout"}))
        self.assertEqual(MODULE.scan(path), [])
