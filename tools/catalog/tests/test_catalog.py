import json
import unittest
from pathlib import Path

from tools.catalog.validate_catalog import CatalogError, FAMILIES, main, validate_catalog


ROOT = Path(__file__).resolve().parents[3]


class CatalogTests(unittest.TestCase):
    def test_canonical_cli_needs_no_path_argument(self) -> None:
        self.assertEqual(main([]), 0)

    def test_release_catalog_covers_all_families(self) -> None:
        document = json.loads((ROOT / "data/poison/catalog/tools.json").read_text(encoding="utf-8"))
        tools = validate_catalog(document)["tools"]
        self.assertEqual({tool["family"] for tool in tools}, FAMILIES)
        self.assertGreaterEqual(len(tools), len(FAMILIES))

    def test_duplicate_ids_and_missing_family_are_rejected(self) -> None:
        document = json.loads((ROOT / "data/poison/catalog/tools.json").read_text(encoding="utf-8"))
        document["tools"].append(dict(document["tools"][0]))
        with self.assertRaises(CatalogError):
            validate_catalog(document)
        document = json.loads((ROOT / "data/poison/catalog/tools.json").read_text(encoding="utf-8"))
        document["tools"][0]["family"] = "unknown"
        with self.assertRaises(CatalogError):
            validate_catalog(document)


if __name__ == "__main__":
    unittest.main()
