from __future__ import annotations

import re
from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]

POISON_STARTUP_MANIFESTS = (
    "applications/services/poison_app/application.fam",
    "applications/services/poison_evidence/application.fam",
    "applications/services/poison_lessons/application.fam",
    "applications/services/poison_packages/application.fam",
    "applications/services/poison_profiles/application.fam",
    "applications/services/poison_rust_api/application.fam",
    "applications/services/poison_tools/application.fam",
    "applications/services/poison_workload/application.fam",
    "applications/system/poison_recovery/application.fam",
)


class PoisonFirmwareStartupTests(unittest.TestCase):
    def test_returning_poison_hooks_are_startup_apps_not_services(self) -> None:
        for relative_path in POISON_STARTUP_MANIFESTS:
            with self.subTest(manifest=relative_path):
                manifest_path = REPOSITORY_ROOT / relative_path
                manifest = manifest_path.read_text(encoding="utf-8")
                self.assertIn("apptype=FlipperAppType.STARTUP", manifest)
                self.assertNotIn("apptype=FlipperAppType.SERVICE", manifest)

                entry_point_match = re.search(
                    r'entry_point="(?P<entry_point>[^"]+)"', manifest
                )
                self.assertIsNotNone(entry_point_match)
                entry_point = entry_point_match.group("entry_point")
                self.assertTrue(entry_point.endswith("_on_system_start"))

                source_files = tuple(manifest_path.parent.glob("*.c"))
                self.assertTrue(source_files)
                definition = re.compile(
                    rf"\bvoid\s+{re.escape(entry_point)}\s*\(\s*void\s*\)"
                )
                self.assertTrue(
                    any(
                        definition.search(source.read_text(encoding="utf-8"))
                        for source in source_files
                    ),
                    f"{entry_point} must retain the startup-hook ABI",
                )


if __name__ == "__main__":
    unittest.main()
