import hashlib
import importlib.util
import json
import tempfile
import types
import unittest
import zipfile
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts" / "marauder.py"


def load_module():
    spec = importlib.util.spec_from_file_location("poison_marauder", MODULE_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class MarauderProvisioningTest(unittest.TestCase):
    def setUp(self):
        self.module = load_module()
        self.payload = b"verified-marauder-segment"
        self.digest = hashlib.sha256(self.payload).hexdigest()
        self.lock = {
            "schemaVersion": 1,
            "sourceRepository": "justcallmekoko/ESP32Marauder",
            "sourceCommit": "6e375e377abb70084720484e9b25de485627f688",
            "version": "v1.15.0",
            "channel": "stable",
        }
        self.manifest = {
            "schemaVersion": 1,
            "kind": "esp32-marauder-installer-release",
            "metadataStatus": "authoritative",
            "sourceRepository": self.lock["sourceRepository"],
            "sourceCommit": self.lock["sourceCommit"],
            "version": self.lock["version"],
            "channel": self.lock["channel"],
            "targets": [
                {
                    "id": "flipper-zero-wifi-dev-board",
                    "displayName": "Flipper Zero WiFi Dev Board",
                    "aliases": ["Flipper Zero Dev Board", "WiFi Dev Board"],
                    "chipFamily": "ESP32-S2",
                    "esptoolChip": "esp32s2",
                    "flash": {
                        "sizeBytes": 4 * 1024 * 1024,
                        "mode": "dio",
                        "frequency": "80m",
                        "factory": {
                            "erase": True,
                            "preservesUserData": False,
                            "segments": [
                                {
                                    "role": "application",
                                    "offset": 0x10000,
                                    "size": len(self.payload),
                                    "sha256": self.digest,
                                    "fileName": "marauder.bin",
                                }
                            ],
                        },
                    },
                },
                {
                    "id": "reverse-feather",
                    "displayName": "ESP32-S2 Reverse Feather",
                    "aliases": [],
                    "chipFamily": "ESP32-S2",
                    "esptoolChip": "esp32s2",
                    "flash": {
                        "sizeBytes": 4 * 1024 * 1024,
                        "mode": "dio",
                        "frequency": "80m",
                        "factory": {
                            "erase": True,
                            "preservesUserData": False,
                            "segments": [
                                {
                                    "role": "application",
                                    "offset": 0x10000,
                                    "size": len(self.payload),
                                    "sha256": self.digest,
                                    "fileName": "reverse.bin",
                                }
                            ],
                        },
                    },
                },
            ],
        }

    def write_manifest(self, directory, manifest=None):
        path = Path(directory) / "firmware-manifest.json"
        path.write_text(json.dumps(manifest or self.manifest), encoding="utf-8")
        return path

    def test_cli_parser_registers_global_cache_option_once(self):
        parser = self.module.build_parser()
        cache_actions = [
            action for action in parser._actions if "--cache" in action.option_strings
        ]
        self.assertEqual(len(cache_actions), 1)

    def test_manifest_must_match_authoritative_pinned_release(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            manifest = dict(self.manifest)
            manifest["metadataStatus"] = "generated-unverified"
            path = self.write_manifest(temp_dir, manifest)
            with self.assertRaisesRegex(self.module.ManifestError, "authoritative"):
                self.module.load_manifest(path, self.lock)

            manifest["metadataStatus"] = "authoritative"
            manifest["sourceCommit"] = "0" * 40
            path = self.write_manifest(temp_dir, manifest)
            with self.assertRaisesRegex(self.module.ManifestError, "sourceCommit"):
                self.module.load_manifest(path, self.lock)

    def test_hardware_name_selects_exact_profile(self):
        target = self.module.select_target(
            self.manifest,
            hardware_name="Flipper Zero Dev Board",
            chip_family="ESP32-S2",
            flash_size=4 * 1024 * 1024,
        )
        self.assertEqual(target["id"], "flipper-zero-wifi-dev-board")

    def test_chip_family_alone_refuses_ambiguous_profile(self):
        with self.assertRaisesRegex(
            self.module.AmbiguousTargetError, "reverse-feather"
        ):
            self.module.select_target(
                self.manifest,
                chip_family="ESP32-S2",
                flash_size=4 * 1024 * 1024,
            )

    def test_explicit_profile_still_checks_detected_chip(self):
        with self.assertRaisesRegex(self.module.TargetMismatchError, "ESP32-S2"):
            self.module.select_target(
                self.manifest,
                requested="flipper-zero-wifi-dev-board",
                chip_family="ESP32",
                flash_size=4 * 1024 * 1024,
            )

    def test_every_segment_is_size_and_hash_verified(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "marauder.bin").write_bytes(self.payload)
            target = self.manifest["targets"][0]
            verified = self.module.verify_target_assets(target, root, "factory")
            self.assertEqual(verified[0].path, root / "marauder.bin")

            (root / "marauder.bin").write_bytes(self.payload + b"tampered")
            with self.assertRaisesRegex(self.module.AssetVerificationError, "size"):
                self.module.verify_target_assets(target, root, "factory")

    def test_manifest_rejects_segment_path_traversal(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            manifest = json.loads(json.dumps(self.manifest))
            manifest["targets"][0]["flash"]["factory"]["segments"][0][
                "fileName"
            ] = "../marauder.bin"
            path = self.write_manifest(temp_dir, manifest)
            with self.assertRaisesRegex(self.module.ManifestError, "fileName"):
                self.module.load_manifest(path, self.lock)

    def test_extracted_cache_member_must_still_match_verified_bundle(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            bundle = root / "bundle.zip"
            with zipfile.ZipFile(bundle, "w") as archive:
                archive.writestr("firmware-manifest.json", b"verified manifest")
            release = root / "release"
            assets = release / "assets"
            assets.mkdir(parents=True)
            (assets / "firmware-manifest.json").write_bytes(b"substituted manifest")

            with self.assertRaisesRegex(
                self.module.AssetVerificationError, "extracted cache member"
            ):
                self.module._extract_bundle_without_overwrite(bundle, release)

    def test_info_and_esptool_output_are_parsed_without_guessing(self):
        info = self.module.parse_marauder_info(
            "Version: v1.14.3\r\nHardware: Flipper Zero Dev Board\r\n"
        )
        self.assertEqual(info.hardware_name, "Flipper Zero Dev Board")
        self.assertEqual(info.version, "v1.14.3")
        self.assertIsNone(self.module.parse_marauder_info("random serial noise"))

        self.assertEqual(
            self.module.parse_esptool_chip("Chip is ESP32-S2 (revision v1.0)"),
            "ESP32-S2",
        )

    def test_factory_commands_erase_write_and_verify_exact_segments(self):
        target = self.manifest["targets"][0]
        asset = self.module.VerifiedSegment(
            role="application",
            offset=0x10000,
            path=Path("/tmp/marauder.bin"),
            size=len(self.payload),
            sha256=self.digest,
        )
        commands = self.module.build_esptool_commands(
            python="/toolchain/python3",
            port="/dev/cu.flipper",
            target=target,
            mode="factory",
            segments=[asset],
            baud=460800,
        )
        self.assertEqual(commands[0][-1], "erase_flash")
        self.assertIn("write_flash", commands[1])
        self.assertIn("0x10000", commands[1])
        self.assertIn("verify_flash", commands[2])

    def test_provision_always_exits_bridge_after_ambiguous_detection(self):
        args = types.SimpleNamespace(
            lock=Path("lock.json"),
            cache=Path("cache"),
            port="/dev/cu.usbmodemflip_Osprit1",
            python="/toolchain/python3",
            target=None,
            baud=460800,
        )
        with (
            mock.patch.object(
                self.module,
                "prepare_assets",
                return_value=(self.lock, self.manifest, Path("assets")),
            ),
            mock.patch.object(
                self.module,
                "find_flipper_port",
                return_value=args.port,
            ),
            mock.patch.object(
                self.module,
                "start_marauder_bridge",
                return_value=args.port,
            ),
            mock.patch.object(
                self.module,
                "query_marauder_info",
                return_value=(None, ""),
            ),
            mock.patch.object(
                self.module,
                "detect_esp",
                return_value=("ESP32-S2", 4 * 1024 * 1024),
            ),
            mock.patch.object(self.module, "exit_marauder_bridge") as exit_bridge,
        ):
            with self.assertRaises(self.module.AmbiguousTargetError):
                self.module.bridge_provision(args)

        exit_bridge.assert_called_once_with(args.port)

    def test_gpio_app_has_dedicated_headless_marauder_bridge(self):
        app_source = (ROOT / "applications/main/gpio/gpio_app.c").read_text(
            encoding="utf-8"
        )
        scene_source = (
            ROOT / "applications/main/gpio/scenes/gpio_scene_usb_uart.c"
        ).read_text(encoding="utf-8")
        header_source = (ROOT / "applications/main/gpio/gpio_app_i.h").read_text(
            encoding="utf-8"
        )
        self.assertIn('strcmp(p, "marauder_bridge") == 0', app_source)
        self.assertIn("marauder_bridge", header_source)
        self.assertIn("scene_usb_uart->cfg.flow_pins = 2;", scene_source)
        self.assertIn("scene_usb_uart->cfg.uart_ch = 0;", scene_source)
        self.assertIn("power_enable_otg(app->power, true);", scene_source)

    def test_fbt_exposes_verified_prepare_and_flash_targets(self):
        source = (ROOT / "SConstruct").read_text(encoding="utf-8")
        self.assertIn('"marauder_prepare"', source)
        self.assertIn('"marauder_flash"', source)
        self.assertIn('"${FBT_SCRIPT_DIR}/marauder.py"', source)
        self.assertIn("poison_esp_flasher_fap", source)

    def test_requested_port_must_be_real_flipper_descriptor(self):
        display_link = types.SimpleNamespace(
            device="/dev/cu.displaylink", vid=0x17E9, pid=0x6000
        )
        serial_module = types.SimpleNamespace(
            tools=types.SimpleNamespace(
                list_ports=types.SimpleNamespace(comports=lambda: [display_link])
            )
        )
        with mock.patch.object(
            self.module, "_serial_module", return_value=serial_module
        ):
            with self.assertRaisesRegex(self.module.TransportError, "not a Flipper"):
                self.module.find_flipper_port(display_link.device)

    def test_normal_provision_stages_and_launches_on_device_flasher(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            fap = Path(temp_dir) / "poison_esp_flasher.fap"
            fap.write_bytes(b"fap")
            segment = self.module.VerifiedSegment(
                role="application",
                offset=0x10000,
                path=Path(temp_dir) / "marauder.bin",
                size=len(self.payload),
                sha256=self.digest,
            )
            segment.path.write_bytes(self.payload)
            args = types.SimpleNamespace(
                lock=Path("lock.json"),
                cache=Path("cache"),
                port="/dev/cu.usbmodemflip_Osprit1",
                target="flipper-zero-wifi-dev-board",
                fap=fap,
            )
            rpc = mock.MagicMock()
            rpc.__enter__.return_value = rpc
            rpc.read_file.return_value = b"ok\n"

            with (
                mock.patch.object(
                    self.module,
                    "prepare_assets",
                    return_value=(self.lock, self.manifest, Path(temp_dir)),
                ),
                mock.patch.object(
                    self.module, "_on_device_segments", return_value=[segment]
                ),
                mock.patch.object(
                    self.module, "find_flipper_port", return_value=args.port
                ),
                mock.patch.object(
                    self.module, "FlipperRpc", return_value=rpc
                ),
                mock.patch.object(self.module, "run_checked") as host_esptool,
            ):
                self.assertEqual(self.module.provision(args), 0)

            host_esptool.assert_not_called()
            rpc.upload_file.assert_any_call(
                fap, self.module.ON_DEVICE_FAP_PATH
            )
            rpc.upload_file.assert_any_call(
                segment.path, self.module.ON_DEVICE_SEGMENT_PATHS[0x10000]
            )
            rpc.delete_tree.assert_called_once_with(self.module.ON_DEVICE_STATUS_PATH)
            rpc.start_app.assert_called_once_with(
                self.module.ON_DEVICE_FAP_PATH, "marauder_flipper"
            )
            rpc.read_file.assert_called_once_with(self.module.ON_DEVICE_STATUS_PATH)

    def test_no_target_launches_hardware_selection_without_auto_flash(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            fap = Path(temp_dir) / "poison_esp_flasher.fap"
            fap.write_bytes(b"fap")
            args = types.SimpleNamespace(
                lock=Path("lock.json"),
                cache=Path("cache"),
                port="/dev/cu.usbmodemflip_Osprit1",
                target=None,
                fap=fap,
            )
            rpc = mock.MagicMock()
            rpc.__enter__.return_value = rpc

            with (
                mock.patch.object(
                    self.module,
                    "prepare_assets",
                    return_value=(self.lock, self.manifest, Path(temp_dir)),
                ),
                mock.patch.object(self.module, "_on_device_segments", return_value=[]),
                mock.patch.object(
                    self.module, "find_flipper_port", return_value=args.port
                ),
                mock.patch.object(
                    self.module, "FlipperRpc", return_value=rpc
                ),
            ):
                self.assertEqual(self.module.provision(args), 0)

            rpc.start_app.assert_called_once_with(
                self.module.ON_DEVICE_FAP_PATH, "select_board"
            )
            rpc.delete_tree.assert_not_called()
            rpc.read_file.assert_not_called()

    def test_on_device_flash_error_marker_fails_provisioning(self):
        rpc = mock.MagicMock()
        rpc.__enter__.return_value = rpc
        rpc.read_file.return_value = b"error:7\n"
        with mock.patch.object(self.module, "FlipperRpc", return_value=rpc):
            with self.assertRaisesRegex(self.module.TransportError, "error:7"):
                self.module._wait_for_on_device_flash("/dev/cu.fixture")

    def test_external_app_resources_are_accepted_and_distributed(self):
        manifest_loader = (ROOT / "scripts/fbt/appmanifest.py").read_text(
            encoding="utf-8"
        )
        resource_builder = (ROOT / "scripts/fbt_tools/fbt_resources.py").read_text(
            encoding="utf-8"
        )
        flasher_manifest = (
            ROOT / "applications/external/poison_esp_flasher/application.fam"
        ).read_text(encoding="utf-8")

        self.assertIn('resources="resources"', flasher_manifest)
        self.assertIn("for app_property in ():", manifest_loader)
        self.assertIn('env["FW_EXTAPPS"].application_map.values()', resource_builder)

    def test_on_device_flasher_uses_fixed_usart_and_md5_verification(self):
        app_dir = ROOT / "applications/external/poison_esp_flasher"
        uart = (app_dir / "esp_flasher_uart.h").read_text(encoding="utf-8")
        worker = (app_dir / "esp_flasher_worker.c").read_text(encoding="utf-8")
        app = (app_dir / "esp_flasher_app.c").read_text(encoding="utf-8")

        self.assertIn("FuriHalSerialIdUsart", uart)
        self.assertNotIn("momentum_settings", uart)
        self.assertIn("esp_loader_flash_verify();", worker)
        self.assertIn("static esp_loader_error_t _flash_all_files", worker)
        self.assertIn("err = _flash_all_files(app);", worker)
        self.assertIn("if(err == ESP_LOADER_SUCCESS) {", worker)
        self.assertIn('"Flash failed with error: %u\\n"', worker)
        self.assertIn('strcmp(p, "marauder_flipper") == 0', app)
        self.assertIn('strcmp(p, "select_board") == 0', app)
        self.assertIn("ESP_MARAUDER_STATUS_PATH", worker)
        self.assertIn('esp_flasher_write_automation_status(app, "running\\n")', worker)
        self.assertIn('strlcpy(status, "ok\\n"', worker)

    def test_marauder_companion_uses_full_bounded_driver_with_active_confirmation(self):
        app_dir = ROOT / "applications" / "external" / "poison_marauder"
        category_manifest = (
            ROOT / "applications" / "external" / "application.fam"
        ).read_text(encoding="utf-8")
        app_search = (ROOT / "site_scons" / "commandline.scons").read_text(
            encoding="utf-8"
        )
        manifest = (app_dir / "application.fam").read_text(encoding="utf-8")
        source = (app_dir / "poison_marauder.c").read_text(encoding="utf-8")
        driver = (
            ROOT
            / "applications"
            / "drivers"
            / "esp32marauder"
            / "esp32_marauder_driver.c"
        ).read_text(encoding="utf-8")
        self.assertIn('appid="poison_external_apps"', category_manifest)
        self.assertIn('("applications/external", False)', app_search)
        self.assertIn('appid="poison_marauder"', manifest)
        self.assertIn('fap_category="GPIO"', manifest)
        self.assertIn('OBSERVE("info"', driver)
        self.assertIn('CONTROL("scan.all"', driver)
        self.assertIn('CAPTURE("sniff.pmkid"', driver)
        self.assertIn('ACTIVE("attack.deauth"', driver)
        self.assertIn("esp32_marauder_command_format", source)
        self.assertIn("poison_marauder_confirm_command", source)


if __name__ == "__main__":
    unittest.main()
