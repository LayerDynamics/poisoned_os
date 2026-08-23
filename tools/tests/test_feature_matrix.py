from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "verify_feature_matrix.py"


def load_module():
    spec = importlib.util.spec_from_file_location("verify_feature_matrix", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {MODULE_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FeatureMatrixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.module = load_module()

    def test_repository_matrix_has_local_core_capabilities(self) -> None:
        matrix = self.module.validate_matrix(
            self.module._load_json(ROOT / "config/features/local-only.json")
        )
        features = {feature["id"]: feature for feature in matrix["features"]}
        for feature_id in ("device-control", "files", "evidence-export", "customization", "installed-workloads"):
            self.assertEqual(features[feature_id]["local"], "enabled")
            self.assertFalse(features[feature_id]["accountRequired"])
            self.assertEqual(features[feature_id]["hostedDependency"], "none")

    def test_local_only_rejects_account_required_profile(self) -> None:
        matrix = self.module._load_json(ROOT / "config/features/local-only.json")
        matrix["profiles"]["local-only"]["accountRequired"] = True
        with self.assertRaisesRegex(self.module.FeatureMatrixError, "cannot require an account"):
            self.module.validate_matrix(matrix)

    def test_local_enabled_feature_cannot_use_hosted_dependency(self) -> None:
        matrix = self.module._load_json(ROOT / "config/features/local-only.json")
        matrix["features"][0]["hostedDependency"] = "required"
        with self.assertRaisesRegex(self.module.FeatureMatrixError, "cannot require a hosted dependency"):
            self.module.validate_matrix(matrix)

    def test_duplicate_feature_ids_are_rejected(self) -> None:
        matrix = self.module._load_json(ROOT / "config/features/local-only.json")
        matrix["features"].append(dict(matrix["features"][0]))
        with self.assertRaisesRegex(self.module.FeatureMatrixError, "duplicate feature id"):
            self.module.validate_matrix(matrix)

    def test_local_only_source_scan_rejects_hosted_import_and_requests(self) -> None:
        matrix = self.module._load_json(ROOT / "config/features/local-only.json")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_dir = root / "dashboard"
            source_dir.mkdir()
            (source_dir / "client.ts").write_text(
                "import hosted_client from './hosted_client';\nfetch('/catalog');\n",
                encoding="utf-8",
            )
            errors = self.module.scan_local_only_sources(root, matrix)
        self.assertEqual(len(errors), 2)
        self.assertIn("hosted_client", errors[0])
        self.assertIn("fetch(", errors[1])

    def test_valid_matrix_round_trips_as_json(self) -> None:
        matrix = self.module._load_json(ROOT / "config/features/local-only.json")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "features.json"
            path.write_text(json.dumps(matrix), encoding="utf-8")
            self.assertEqual(
                self.module.validate_matrix(self.module._load_json(path))["schema"],
                "poison.features/v1",
            )


if __name__ == "__main__":
    unittest.main()
