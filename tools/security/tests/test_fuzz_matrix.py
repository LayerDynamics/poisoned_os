import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("run_fuzz_matrix", ROOT / "tools/security/run_fuzz_matrix.py")
MODULE = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(MODULE)


class FuzzMatrixTests(unittest.TestCase):
    def test_missing_target_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            result = MODULE.run_matrix(Path(directory))
            self.assertEqual(result["result"], "FAIL")

    def test_all_targets_are_run_and_hashed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for manifest, _ in MODULE.TARGETS.values():
                path = root / manifest; path.parent.mkdir(parents=True, exist_ok=True); path.write_text("[package]\nname='fuzz'\nversion='0.1.0'\n", encoding="utf-8")
            completed = mock.Mock(returncode=0, stdout="ok", stderr="")
            with mock.patch.object(MODULE.subprocess, "run", return_value=completed):
                result = MODULE.run_matrix(root)
            self.assertEqual(result["result"], "PASS")
            self.assertEqual(len(result["targets"]), 4)
