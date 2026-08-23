from __future__ import annotations

import re
from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]

POISON_STARTUP_HOOKS = (
    (
        "applications/services/rpc/application.fam",
        "applications/services/rpc/rpc.c",
        "rpc_on_system_start",
    ),
    (
        "applications/services/poison_audit/application.fam",
        "applications/services/poison_audit/poison_audit.c",
        "poison_audit_on_system_start",
    ),
    (
        "applications/services/poison_diagnostics/application.fam",
        "applications/services/poison_diagnostics/poison_diagnostics.c",
        "poison_diagnostics_on_system_start",
    ),
    (
        "applications/services/poison_vfs/application.fam",
        "applications/services/poison_vfs/poison_vfs.c",
        "poison_vfs_on_system_start",
    ),
    (
        "applications/services/poison_rust_api/application.fam",
        "applications/services/poison_rust_api/poison_rust_api.c",
        "poison_rust_api_on_system_start",
    ),
    (
        "applications/drivers/esp32marauder/application.fam",
        "applications/drivers/esp32marauder/esp32_marauder_driver.c",
        "esp32_marauder_driver_start",
    ),
    (
        "applications/services/poison_evidence/application.fam",
        "applications/services/poison_evidence/poison_evidence.c",
        "poison_evidence_on_system_start",
    ),
    (
        "applications/services/poison_packages/application.fam",
        "applications/services/poison_packages/poison_packages.c",
        "poison_packages_on_system_start",
    ),
    (
        "applications/services/poison_app/application.fam",
        "applications/services/poison_app/poison_app.c",
        "poison_app_on_system_start",
    ),
    (
        "applications/services/poison_profiles/application.fam",
        "applications/services/poison_profiles/poison_profiles.c",
        "poison_profiles_on_system_start",
    ),
    (
        "applications/services/poison_tools/application.fam",
        "applications/services/poison_tools/poison_tools.c",
        "poison_tools_on_system_start",
    ),
    (
        "applications/services/poison_workload/application.fam",
        "applications/services/poison_workload/poison_workload.c",
        "poison_workload_on_system_start",
    ),
    (
        "applications/services/poison_lessons/application.fam",
        "applications/services/poison_lessons/poison_lessons.c",
        "poison_lessons_on_system_start",
    ),
    (
        "applications/system/poison_recovery/application.fam",
        "applications/system/poison_recovery/poison_recovery.c",
        "poison_recovery_on_system_start",
    ),
)


class PoisonFirmwareStartupTests(unittest.TestCase):
    def test_returning_poison_hooks_are_startup_apps_not_services(self) -> None:
        for manifest_name, source_name, expected_entry_point in POISON_STARTUP_HOOKS:
            with self.subTest(manifest=manifest_name):
                manifest_path = REPOSITORY_ROOT / manifest_name
                manifest = manifest_path.read_text(encoding="utf-8")
                self.assertIn("apptype=FlipperAppType.STARTUP", manifest)
                self.assertNotIn("apptype=FlipperAppType.SERVICE", manifest)

                entry_point_match = re.search(
                    r'entry_point="(?P<entry_point>[^"]+)"', manifest
                )
                self.assertIsNotNone(entry_point_match)
                entry_point = entry_point_match.group("entry_point")
                self.assertEqual(entry_point, expected_entry_point)

                source = (REPOSITORY_ROOT / source_name).read_text(encoding="utf-8")
                definition = re.compile(
                    rf"\bvoid\s+{re.escape(entry_point)}\s*\(\s*void\s*\)"
                )
                self.assertIsNotNone(
                    definition.search(source),
                    f"{entry_point} must retain the startup-hook ABI",
                )

    def test_poison_runtime_hooks_skip_firmware_update_boots(self) -> None:
        for _, source_name, entry_point in POISON_STARTUP_HOOKS:
            with self.subTest(source=source_name):
                source = (REPOSITORY_ROOT / source_name).read_text(encoding="utf-8")
                guarded_function = (
                    "esp32_marauder_driver_on_system_start"
                    if entry_point == "esp32_marauder_driver_start"
                    else entry_point
                )
                definition = re.search(
                    rf"\bvoid\s+{re.escape(guarded_function)}\s*\([^)]*\)\s*\{{",
                    source,
                )
                self.assertIsNotNone(definition)
                function_prefix = source[definition.end() : definition.end() + 256]
                self.assertIn(
                    "if(!poison_startup_is_runtime_boot()) return;",
                    function_prefix,
                    f"{guarded_function} must not initialize during updater boots",
                )

    def test_runtime_boot_guard_only_accepts_normal_boot(self) -> None:
        startup_header = (
            REPOSITORY_ROOT / "applications/services/poison_startup.h"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "return furi_hal_rtc_get_boot_mode() == FuriHalRtcBootModeNormal;",
            startup_header,
        )


if __name__ == "__main__":
    unittest.main()
