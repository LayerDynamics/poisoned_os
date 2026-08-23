import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("verify_spec", ROOT / "tools/release/verify_spec.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class VerifySpecTests(unittest.TestCase):
    def test_missing_ledger_reports_all_requirements(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            spec = root / "spec.md"
            spec.write_text("FR-1 MUST work\nFR-2 MUST recover\n", encoding="utf-8")
            problems = MODULE.verify(spec, root / "evidence")
            self.assertIn("missing evidence for FR-1", problems)
            self.assertIn("missing evidence for FR-2", problems)

    def test_duplicate_missing_and_synthetic_entries_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = root / "evidence"
            evidence.mkdir()
            (evidence / "one.json").write_text("{}", encoding="utf-8")
            spec = root / "spec.md"
            spec.write_text("FR-1 MUST work\n", encoding="utf-8")
            entry = {"id": "FR-1", "milestone": "M6", "command": "test", "evidence": "one.json", "version": "1.0.0", "result": "PASS", "reviewer": "test", "timestamp": "2026-08-21T00:00:00Z", "physicalIds": ["REPLACE_ME"]}
            (evidence / "v1-requirements.json").write_text(json.dumps({"schema": MODULE.SCHEMA, "requirements": [entry, entry]}), encoding="utf-8")
            problems = MODULE.verify(spec, evidence)
            self.assertTrue(any("duplicate" in problem for problem in problems))
            self.assertTrue(any("synthetic" in problem for problem in problems))
