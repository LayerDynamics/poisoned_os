import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("run_adversarial_suite", ROOT / "tools/security/run_adversarial_suite.py")
MODULE = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(MODULE)


class SecurityMatrixTests(unittest.TestCase):
    def test_invalid_unowned_check_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "matrix.json"; path.write_text(json.dumps({"schema": "poison.security-matrix/v1", "findings": [], "checks": [{"id": "bad", "command": ["true"]}]}), encoding="utf-8")
            result = MODULE.run_matrix(Path(directory), path)
            self.assertEqual(result["result"], "FAIL")

    def test_passes_executable_owned_checks(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "matrix.json"; path.write_text(json.dumps({"schema": "poison.security-matrix/v1", "findings": [], "checks": [{"id": "check", "owner": "test", "command": ["true"]}]}), encoding="utf-8")
            result = MODULE.run_matrix(Path(directory), path)
            self.assertEqual(result["result"], "PASS")
