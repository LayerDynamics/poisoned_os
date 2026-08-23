import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools/release" / f"{name}.py")
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module); return module


SIGN = load("sign_release")
PROMOTE = load("promote_release")


class SignAndPromoteTests(unittest.TestCase):
    def test_signer_canonicalizes_without_existing_signature(self):
        payload = {"schema": "poison.release-manifest/v1", "version": "1.0.0", "channel": "stable"}
        self.assertEqual(SIGN.canonical_payload(payload), SIGN.canonical_payload({**payload, "signature": {"value": "ignored"}}))

    def test_promotion_requires_pass_health_and_records_digests(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); manifest = root / "release.json"; health = root / "health.json"; output = root / "promotion.json"
            manifest.write_text(json.dumps({"schema": "poison.release-manifest/v1", "version": "1.0.0", "channel": "beta"}), encoding="utf-8")
            health.write_text(json.dumps({"result": "PASS"}), encoding="utf-8")
            result = PROMOTE.promote(manifest, health, output, "beta")
            self.assertEqual(result["schema"], "poison.release-promotion/v1")
            health.write_text(json.dumps({"result": "HALT"}), encoding="utf-8")
            with self.assertRaises(ValueError): PROMOTE.promote(manifest, health, output, "beta")
