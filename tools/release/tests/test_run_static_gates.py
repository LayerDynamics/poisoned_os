import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("run_static_gates", ROOT / "tools/release/run_static_gates.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class StaticGateTests(unittest.TestCase):
    def test_rejects_duplicate_gate_ids(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "gates.json"
            path.write_text('{"schema":"poison.release-static-gates/v1","profiles":{"stable":[{"id":"x","command":["true"]},{"id":"x","command":["true"]}]}}', encoding="utf-8")
            with self.assertRaises(MODULE.GateError):
                MODULE.load_config(path, "stable")

    def test_records_pass_and_missing_tool(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            results = MODULE.run_gates(root, [{"id": "pass", "command": ["true"]}, {"id": "missing", "command": ["poison-command-does-not-exist"]}])
            self.assertEqual(results["result"], "FAIL")
            self.assertEqual(results["gates"][0]["status"], "PASS")
            self.assertEqual(results["gates"][1]["reason"], "tool unavailable")
