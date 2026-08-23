import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("compare_budgets", ROOT / "tools/release/compare_budgets.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class CompareBudgetsTests(unittest.TestCase):
    def write(self, value: dict) -> Path:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "performance.json"
        path.write_text(json.dumps(value), encoding="utf-8")
        return path

    def test_passes_declared_p95_and_sample_budget(self):
        result = MODULE.compare(self.write({"schema": "poison.performance/v1", "measurements": [{"id": "usb-command", "values": [10, 12, 11, 13], "budget": {"samples": 4, "p95": 20, "max": 20}}]}))
        self.assertEqual(result["result"], "PASS")

    def test_fails_percentile_and_insufficient_samples(self):
        result = MODULE.compare(self.write({"schema": "poison.performance/v1", "measurements": [{"id": "ble-command", "values": [10, 100], "budget": {"samples": 3, "p95": 50}}]}))
        self.assertEqual(result["result"], "FAIL")
        self.assertTrue(any("insufficient" in failure for failure in result["failures"]))
        self.assertTrue(any("p95" in failure for failure in result["failures"]))

    def test_rejects_duplicate_measurements(self):
        with self.assertRaises(MODULE.BudgetError):
            MODULE.compare(self.write({"schema": "poison.performance/v1", "measurements": [{"id": "same", "values": [1], "budget": {"samples": 1}}, {"id": "same", "values": [1], "budget": {"samples": 1}}]}))
