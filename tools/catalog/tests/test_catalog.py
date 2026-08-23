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

    def test_live_subghz_policy_does_not_put_profile_on_worker_stack(self) -> None:
        source = (ROOT / "applications/services/poison_tools/poison_tools.c").read_text(
            encoding="utf-8"
        )
        start = source.index("static bool poison_tools_subghz_policy_snapshot(")
        end = source.index("static bool poison_tools_execute_subghz_receive(", start)
        callback = source[start:end]
        self.assertNotIn("PoisonProfile active;", callback)
        self.assertIn("PoisonProfile* active = malloc(sizeof(*active));", callback)
        self.assertIn("memset(active, 0, sizeof(*active));", callback)
        self.assertIn("free(active);", callback)

    def test_subghz_callback_finishes_decode_before_signaling_completion(self) -> None:
        source = (
            ROOT / "applications/services/poison_tools/poison_subghz_adapter.c"
        ).read_text(encoding="utf-8")
        start = source.index("static void poison_subghz_pair(")
        end = source.index("static void poison_subghz_overrun(", start)
        callback = source[start:end]
        received_start = source.index("static void poison_subghz_received(")
        received_end = source.index("static void poison_subghz_pair(", received_start)
        self.assertNotIn("furi_semaphore_release(", source[received_start:received_end])
        self.assertLess(
            callback.index("subghz_receiver_decode("),
            callback.index("furi_semaphore_release("),
        )
        document = json.loads((ROOT / "data/poison/catalog/tools.json").read_text(encoding="utf-8"))
        document["tools"][0]["family"] = "unknown"
        with self.assertRaises(CatalogError):
            validate_catalog(document)


if __name__ == "__main__":
    unittest.main()
