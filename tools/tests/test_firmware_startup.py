from __future__ import annotations

import re
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT / "scripts"))
PRODUCTION_ELF_CANDIDATES = (
    REPOSITORY_ROOT / "build/f7-firmware-C/firmware.elf",
    REPOSITORY_ROOT / "dist/f7-C/flipper-z-f7-firmware-poisonedos.elf",
)

from fbt.stack_analyzer import (  # noqa: E402
    StackAnalysisError,
    analyze_startup_stack,
    calculate_rpc_worker_stack,
    calculate_stack_path,
    parse_rpc_handler_sources,
    parse_disassembly,
    parse_dwarf_frames,
    parse_noreturn_functions,
    validate_stack_report,
    validate_startup_order,
)

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
    def test_bundler_rejects_stack_report_for_a_different_elf(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            elf = root / "firmware.elf"
            elf.write_bytes(b"firmware")
            report = root / "firmware.startup-stack.json"
            report.write_text(
                json.dumps(
                    {
                        "schema": "poison.firmware-stack/v2",
                        "passed": True,
                        "stack_budget": 2048,
                        "maximum_stack": 1024,
                        "startup_order": [
                            "storage_start",
                            "rpc_start",
                            "expansion_start",
                        ],
                        "rpc_worker": {
                            "passed": True,
                            "stack_budget": 6144,
                            "maximum_stack": 4096,
                            "handlers": ["rpc_fixture_process"],
                        },
                        "elf_sha256": hashlib.sha256(b"different").hexdigest(),
                    }
                ),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(
                StackAnalysisError, "does not match the firmware ELF"
            ):
                validate_stack_report(report, elf)

    def test_bundler_keeps_stack_report_attached_to_firmware_component(self) -> None:
        source = (REPOSITORY_ROOT / "scripts/sconsdist.py").read_text(encoding="utf-8")
        self.assertIn(
            'filetype not in (\n            "elf",\n            "startup-stack.json",',
            source,
        )

    def test_stack_analyzer_stops_at_compiler_marked_noreturn_functions(self) -> None:
        self.assertEqual(
            parse_noreturn_functions(
                """
 <1><123>: Abbrev Number: 1 (DW_TAG_subprogram)
  DW_AT_name : (indirect string): fatal
  DW_AT_noreturn : 1
 <2><124>: Abbrev Number: 2 (DW_TAG_formal_parameter)
  DW_AT_name : argument
 <1><125>: Abbrev Number: 1 (DW_TAG_subprogram)
  DW_AT_name : ordinary
"""
            ),
            {"fatal"},
        )

    def test_stack_analyzer_calculates_direct_and_synthetic_call_paths(self) -> None:
        frames = parse_dwarf_frames(
            """
FDE cie=00000000 pc=08000000..08000010
  DW_CFA_def_cfa_offset: 40
FDE cie=00000000 pc=08000010..08000020
  DW_CFA_def_cfa_offset: 48
FDE cie=00000000 pc=08000020..08000030
  DW_CFA_def_cfa_offset: 3584
"""
        )
        functions = parse_disassembly(
            """
08000000 <loader_srv>:
 8000000: bl 8000010 <startup_hook>
08000010 <startup_hook>:
 8000010: bl 8000020 <authority_load>
08000020 <authority_load>:
 8000020: bx lr
""",
            frames,
        )
        path, _, _ = calculate_stack_path(
            functions, "loader_srv", {"loader_srv": ("startup_hook",)}
        )
        self.assertEqual(path.bytes, 3672)
        self.assertEqual(
            path.functions, ("loader_srv", "startup_hook", "authority_load")
        )

    def test_startup_order_rejects_rpc_before_storage(self) -> None:
        with self.assertRaisesRegex(
            StackAnalysisError, "storage_start must run before rpc_start"
        ):
            validate_startup_order(("cli", "rpc_start", "storage_start"))

    def test_startup_order_rejects_expansion_before_rpc(self) -> None:
        with self.assertRaisesRegex(
            StackAnalysisError, "rpc_start must run before expansion_start"
        ):
            validate_startup_order(
                ("cli", "storage_start", "expansion_start", "rpc_start")
            )

    def test_startup_order_accepts_storage_rpc_expansion_sequence(self) -> None:
        validate_startup_order(("cli", "storage_start", "rpc_start", "expansion_start"))

    def test_rpc_handler_parser_tracks_registered_callbacks(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "rpc_fixture.c"
            source.write_text(
                """
RpcHandler handler = {
    .message_handler = rpc_fixture_first_process,
    .decode_submessage = rpc_fixture_decode,
};
handler.message_handler = rpc_fixture_second_process;
handler.decode_submessage = NULL;
""",
                encoding="utf-8",
            )
            self.assertEqual(
                parse_rpc_handler_sources((source,)),
                (
                    "rpc_fixture_decode",
                    "rpc_fixture_first_process",
                    "rpc_fixture_second_process",
                ),
            )

    def test_rpc_worker_gate_includes_nested_secure_dispatch(self) -> None:
        frames = parse_dwarf_frames(
            """
FDE cie=00000000 pc=08000000..08000010
  DW_CFA_def_cfa_offset: 64
FDE cie=00000000 pc=08000010..08000020
  DW_CFA_def_cfa_offset: 192
FDE cie=00000000 pc=08000020..08000030
  DW_CFA_def_cfa_offset: 4096
"""
        )
        functions = parse_disassembly(
            """
08000000 <rpc_session_worker>:
 8000000: blx r3
08000010 <rpc_secure_envelope_process>:
 8000010: blx r3
08000020 <rpc_large_handler_process>:
 8000020: bx lr
""",
            frames,
        )
        result = calculate_rpc_worker_stack(
            functions,
            "rpc_session_worker",
            ("rpc_secure_envelope_process", "rpc_large_handler_process"),
            "rpc_secure_envelope_process",
            6144,
        )
        self.assertEqual(result["maximum_stack"], 4352)
        self.assertEqual(
            result["maximum_path"],
            [
                "rpc_session_worker",
                "rpc_secure_envelope_process",
                "rpc_large_handler_process",
            ],
        )

        with self.assertRaisesRegex(
            StackAnalysisError, "RPC worker stack requires 4352"
        ):
            calculate_rpc_worker_stack(
                functions,
                "rpc_session_worker",
                ("rpc_secure_envelope_process", "rpc_large_handler_process"),
                "rpc_secure_envelope_process",
                4096,
            )

    def test_rpc_session_uses_the_shared_validated_stack_budget(self) -> None:
        header = (REPOSITORY_ROOT / "applications/services/rpc/rpc_i.h").read_text(
            encoding="utf-8"
        )
        source = (REPOSITORY_ROOT / "applications/services/rpc/rpc.c").read_text(
            encoding="utf-8"
        )
        self.assertRegex(header, r"#define RPC_SESSION_STACK_SIZE\s+\d+u")
        self.assertIn(
            'furi_thread_alloc_ex(\n        "RpcSessionWorker", RPC_SESSION_STACK_SIZE,',
            source,
        )

    def test_rpc_handlers_do_not_put_expanded_main_messages_on_stack(self) -> None:
        for source in sorted(
            (REPOSITORY_ROOT / "applications/services/rpc").glob("*.c")
        ):
            with self.subTest(source=source.name):
                contents = source.read_text(encoding="utf-8")
                self.assertNotRegex(contents, r"\bPB_Main\s+[A-Za-z_]\w*\s*[;=]")

    def test_authority_decoder_does_not_put_the_full_store_on_stack(self) -> None:
        source = (
            REPOSITORY_ROOT
            / "applications/services/poison_packages/poison_package_authority.c"
        ).read_text(encoding="utf-8")
        self.assertNotIn("PoisonPackageAuthorityStore decoded;", source)

    def test_profile_startup_load_does_not_put_persisted_state_on_stack(self) -> None:
        source = (
            REPOSITORY_ROOT / "applications/services/poison_profiles/poison_profiles.c"
        ).read_text(encoding="utf-8")
        self.assertIn("PoisonProfileState* state = malloc(sizeof(*state));", source)

    @unittest.skipUnless(
        any(path.exists() for path in PRODUCTION_ELF_CANDIDATES),
        "production ELF has not been built",
    )
    def test_production_elf_startup_stack_fits_loader_budget(self) -> None:
        elf = next(path for path in PRODUCTION_ELF_CANDIDATES if path.exists())
        objdump = REPOSITORY_ROOT / "toolchain/arm64-darwin/bin/arm-none-eabi-objdump"
        report = analyze_startup_stack(
            elf,
            str(objdump),
            "loader_srv",
            tuple(hook[2] for hook in POISON_STARTUP_HOOKS),
            2048,
            rpc_sources=tuple(
                sorted((REPOSITORY_ROOT / "applications/services/rpc").glob("*.c"))
            ),
            rpc_root="rpc_session_worker",
            rpc_stack_budget=6144,
            rpc_nested_handler="rpc_poison_session_envelope_process",
            startup_apps=(
                "storage_start",
                "rpc_start",
                "expansion_start",
            ),
        )
        self.assertTrue(report["passed"])
        self.assertLessEqual(
            report["rpc_worker"]["maximum_stack"],
            report["rpc_worker"]["stack_budget"],
        )

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
                    rf"\bvoid\s+{re.escape(guarded_function)}\s*\([^)]*\)" + r"\s*\{",
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
