import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("verify_claims", ROOT / "tools/release/verify_claims.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class VerifyClaimsTests(unittest.TestCase):
    def test_rejects_disallowed_claims(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            adr = root / "adr.md"
            adr.write_text("No external validation is commissioned for V1.\n", encoding="utf-8")
            page = root / "page.md"
            page.write_text("This is court-ready.", encoding="utf-8")
            self.assertTrue(MODULE.scan([page], adr))

    def test_accepts_precise_integrity_language(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            adr = root / "adr.md"
            adr.write_text("No external validation is commissioned for V1.\n", encoding="utf-8")
            page = root / "page.md"
            page.write_text("The export includes a verifiable integrity digest.", encoding="utf-8")
            self.assertEqual(MODULE.scan([page], adr), [])
