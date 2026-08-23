import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("verify_vendor", ROOT / "tools/rust/verify_vendor.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class VerifyVendorTests(unittest.TestCase):
    def make_fixture(self):
        temp = tempfile.TemporaryDirectory()
        root = Path(temp.name)
        vendor = root / "vendor" / "demo-1.0.0"
        vendor.mkdir(parents=True)
        (vendor / "Cargo.toml").write_text("[package]\nname='demo'\n", encoding="utf-8")
        checksum = MODULE._digest(vendor / "Cargo.toml")
        (root / "Cargo.lock").write_text('version = 3\n\n[[package]]\nname = "demo"\nversion = "1.0.0"\n', encoding="utf-8")
        approval = {"schema": MODULE.SCHEMA, "sourcePolicy": {"offline": True}, "crates": [{"name": "demo", "version": "1.0.0", "source": "vendor/demo-1.0.0", "checksum": checksum, "license": "MIT", "features": [], "unsafe": False, "reviewer": "test", "reason": "fixture"}]}
        approval_path = root / "approved.json"
        approval_path.write_text(json.dumps(approval), encoding="utf-8")
        return temp, root, approval_path

    def test_accepts_locked_vendored_crate(self):
        temp, root, approval = self.make_fixture()
        self.addCleanup(temp.cleanup)
        self.assertEqual(MODULE.verify(root, lock_path=root / "Cargo.lock", approval_path=approval, locked=True), 1)

    def test_rejects_unapproved_source(self):
        temp, root, approval = self.make_fixture()
        self.addCleanup(temp.cleanup)
        data = json.loads(approval.read_text(encoding="utf-8"))
        data["crates"][0]["source"] = "git+https://example.invalid/demo"
        approval.write_text(json.dumps(data), encoding="utf-8")
        with self.assertRaisesRegex(MODULE.VendorError, "unapproved source"):
            MODULE.verify(root, lock_path=root / "Cargo.lock", approval_path=approval)

    def test_rejects_checksum_tamper(self):
        temp, root, approval = self.make_fixture()
        self.addCleanup(temp.cleanup)
        data = json.loads(approval.read_text(encoding="utf-8"))
        data["crates"][0]["checksum"] = "0" * 64
        approval.write_text(json.dumps(data), encoding="utf-8")
        with self.assertRaisesRegex(MODULE.VendorError, "checksum mismatch"):
            MODULE.verify(root, lock_path=root / "Cargo.lock", approval_path=approval)


if __name__ == "__main__":
    unittest.main()
