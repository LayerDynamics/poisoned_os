import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("verify_runbooks", ROOT / "tools/release/verify_runbooks.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class VerifyRunbooksTests(unittest.TestCase):
    def test_missing_runbooks_are_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            failures = MODULE.verify(Path(directory))
            self.assertEqual(len(failures), len(MODULE.REQUIRED))

    def test_complete_runbook_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for filename in MODULE.REQUIRED:
                root.joinpath(filename).write_text("# Runbook\n\n" + "\n".join(f"## {section}\nAction\n" for section in MODULE.SECTIONS) + "\n```bash\ntrue\n```\n", encoding="utf-8")
            self.assertEqual(MODULE.verify(root), [])
