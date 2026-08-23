import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("evaluate_rollout_health", ROOT / "tools/release/evaluate_rollout_health.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class EvaluateRolloutHealthTests(unittest.TestCase):
    def test_passes_below_halt_thresholds(self):
        result = MODULE.evaluate({"schema": MODULE.SCHEMA, "metrics": [{"id": "crash_rate", "observed": 0.01, "threshold": 0.05, "haltIf": "gte"}]})
        self.assertEqual(result["result"], "PASS")
        self.assertFalse(result["halted"])

    def test_halts_when_threshold_is_reached(self):
        result = MODULE.evaluate({"schema": MODULE.SCHEMA, "metrics": [{"id": "rollback_rate", "observed": 0.1, "threshold": 0.1, "haltIf": "gte"}]})
        self.assertEqual(result["result"], "HALT")
        self.assertTrue(result["halted"])

    def test_rejects_duplicate_or_invalid_metrics(self):
        result = MODULE.evaluate({"schema": MODULE.SCHEMA, "metrics": [{"id": "x", "observed": 1, "threshold": 2, "haltIf": "bad"}, {"id": "x", "observed": 1, "threshold": 2, "haltIf": "gte"}]})
        self.assertEqual(result["result"], "FAIL")
        self.assertGreaterEqual(len(result["failures"]), 2)
