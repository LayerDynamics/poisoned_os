from __future__ import annotations

import importlib.util
import hashlib
import io
from pathlib import Path
from unittest import mock
import sys
import tempfile
import types
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
STORAGE_MODULE = REPOSITORY_ROOT / "scripts" / "flipper" / "storage.py"
SELFUPDATE_MODULE = REPOSITORY_ROOT / "scripts" / "selfupdate.py"
DEVICE_INSTALL_MODULE = REPOSITORY_ROOT / "scripts" / "device_install.py"
TESTOPS_MODULE = REPOSITORY_ROOT / "scripts" / "testops.py"


class FakeSerialPort:
    def __init__(self) -> None:
        self.port = None
        self.timeout = None
        self.write_timeout = None
        self.baudrate = None
        self.in_waiting = 0
        self.writes: list[bytes] = []
        self.opened = False

    def open(self) -> None:
        self.opened = True

    def close(self) -> None:
        self.opened = False

    def write(self, data: bytes) -> None:
        self.writes.append(data)

    def read(self, size: int) -> bytes:
        return b""

    def reset_input_buffer(self) -> None:
        return None


def load_storage_module():
    serial_module = types.ModuleType("serial")
    serial_module.Serial = FakeSerialPort
    spec = importlib.util.spec_from_file_location(
        "flipper_storage_under_test", STORAGE_MODULE
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, {"serial": serial_module}):
        spec.loader.exec_module(module)
    return module


def load_selfupdate_module():
    flipper_package = types.ModuleType("flipper")
    flipper_app = types.ModuleType("flipper.app")
    flipper_app.App = object
    flipper_storage = types.ModuleType("flipper.storage")
    flipper_storage.FlipperStorage = object
    flipper_storage.FlipperStorageOperations = object
    flipper_utils = types.ModuleType("flipper.utils")
    flipper_cdc = types.ModuleType("flipper.utils.cdc")
    flipper_cdc.resolve_port = lambda *_args: None
    modules = {
        "flipper": flipper_package,
        "flipper.app": flipper_app,
        "flipper.storage": flipper_storage,
        "flipper.utils": flipper_utils,
        "flipper.utils.cdc": flipper_cdc,
    }
    spec = importlib.util.spec_from_file_location(
        "selfupdate_under_test", SELFUPDATE_MODULE
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, modules):
        spec.loader.exec_module(module)
    return module


def load_device_install_module():
    spec = importlib.util.spec_from_file_location(
        "device_install_under_test", DEVICE_INSTALL_MODULE
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_testops_module():
    flipper_package = types.ModuleType("flipper")
    flipper_app = types.ModuleType("flipper.app")
    flipper_app.App = object
    flipper_storage = types.ModuleType("flipper.storage")
    flipper_storage.FlipperStorage = object
    flipper_utils = types.ModuleType("flipper.utils")
    flipper_cdc = types.ModuleType("flipper.utils.cdc")
    flipper_cdc.resolve_port = lambda *_args: None
    serial_package = types.ModuleType("serial")
    serial_util = types.ModuleType("serial.serialutil")
    serial_util.SerialException = Exception
    modules = {
        "flipper": flipper_package,
        "flipper.app": flipper_app,
        "flipper.storage": flipper_storage,
        "flipper.utils": flipper_utils,
        "flipper.utils.cdc": flipper_cdc,
        "serial": serial_package,
        "serial.serialutil": serial_util,
    }
    spec = importlib.util.spec_from_file_location("testops_under_test", TESTOPS_MODULE)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, modules):
        spec.loader.exec_module(module)
    return module


class FlipperStorageStartupTests(unittest.TestCase):
    def test_start_wakes_cli_and_bounds_every_handshake_wait(self) -> None:
        module = load_storage_module()
        storage = module.FlipperStorage("/dev/fixture")
        storage.read = mock.Mock()

        with mock.patch.object(module.time, "sleep"):
            storage.start()

        self.assertTrue(storage.port.opened)
        self.assertEqual(storage.port.writes, [b"\r", b"device_info\r"])
        self.assertEqual(
            storage.read.until.call_args_list,
            [
                mock.call(storage.CLI_PROMPT, timeout=storage.STARTUP_TIMEOUT),
                mock.call("hardware_model", timeout=storage.STARTUP_TIMEOUT),
                mock.call(storage.CLI_PROMPT, timeout=storage.STARTUP_TIMEOUT),
            ],
        )

    def test_start_closes_port_when_handshake_times_out(self) -> None:
        module = load_storage_module()
        storage = module.FlipperStorage("/dev/fixture")
        storage.read = mock.Mock()
        storage.read.until.side_effect = module.FlipperStorageException("timeout")

        with mock.patch.object(module.time, "sleep"):
            with self.assertRaisesRegex(module.FlipperStorageException, "timeout"):
                storage.start()

        self.assertFalse(storage.port.opened)


class SelfUpdateTests(unittest.TestCase):
    def test_closes_running_apps_before_update(self) -> None:
        module = load_selfupdate_module()
        storage = mock.Mock()
        storage.CLI_EOL = b"fixture-eol"
        storage.read.until.side_effect = [
            b'Application "NFC" was closed\r\n',
            b"",
            b"No application is running\r\n",
            b"",
        ]

        with mock.patch.object(module.time, "sleep"):
            result = module.close_running_apps(storage, mock.Mock())

        self.assertTrue(result)
        self.assertEqual(
            storage.send_and_wait_eol.call_args_list,
            [mock.call("loader close\r"), mock.call("loader close\r")],
        )

    def test_refuses_to_force_close_an_uncooperative_app(self) -> None:
        module = load_selfupdate_module()
        storage = mock.Mock()
        storage.CLI_EOL = b"fixture-eol"
        storage.read.until.return_value = (
            b'Application "NFC" has to be closed manually\r\n'
        )

        result = module.close_running_apps(storage, mock.Mock())

        self.assertFalse(result)


class DeviceInstallTests(unittest.TestCase):
    def test_rpc_install_retains_complete_bundle_at_bootstrap_lkg_path(self) -> None:
        module = load_device_install_module()
        rpc = mock.MagicMock()
        rpc.__enter__.return_value = rpc
        rpc.device_info.return_value = {
            "hardware_model": "Flipper Zero",
            "hardware_target": "7",
        }
        with tempfile.TemporaryDirectory() as directory:
            bundle = Path(directory) / "bundle"
            bundle.mkdir()
            manifest = bundle / "update.fuf"
            manifest.write_text("Filetype: Flipper firmware upgrade configuration\n")
            (bundle / "firmware.dfu").write_bytes(b"dfu")
            with mock.patch.object(
                module,
                "detect_flipper_runtime",
                return_value=("/dev/cu.fixture", "flip_fixture"),
            ), mock.patch.object(module, "FlipperRpc", return_value=rpc):
                self.assertEqual(module.run_rpc_update("auto", manifest, []), 0)

        rpc.upload_tree.assert_called_once_with(bundle, "/ext/update/poison-lkg")
        rpc.start_update.assert_called_once_with("/ext/update/poison-lkg/update.fuf")

    def test_normal_flash_target_uses_full_resource_package(self) -> None:
        source = (REPOSITORY_ROOT / "SConstruct").read_text()

        self.assertIn('distenv.Alias("flash_usb", usb_update_package)', source)
        self.assertIn('distenv.Alias("flash_usb_min", usb_minupdate_package)', source)
        self.assertNotIn('distenv.Alias("flash_usb", usb_minupdate_package)', source)
        self.assertIn("poison_esp_flasher_fap,", source)
        self.assertIn('"--marauder-target",', source)
        self.assertIn('POST_INSTALL_ARGS=["--skip-marauder"]', source)
        builder = (
            REPOSITORY_ROOT / "scripts" / "fbt_tools" / "fbt_dist.py"
        ).read_text(encoding="utf-8")
        self.assertIn("*extra_deps,\n        ),\n        **kw,", builder)

    def test_successful_normal_install_provisions_exact_marauder_target(self) -> None:
        module = load_device_install_module()
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "update.fuf"
            manifest.write_text("fixture", encoding="utf-8")
            fap = Path(directory) / "flasher.fap"
            with mock.patch.object(module, "install", return_value=0), mock.patch.object(
                module, "provision_marauder", return_value=0
            ) as provision:
                result = module.main(
                    [
                        "-p",
                        "/dev/cu.fixture",
                        "--marauder-fap",
                        str(fap),
                        str(manifest),
                    ]
                )

        self.assertEqual(result, 0)
        provision.assert_called_once_with(
            "/dev/cu.fixture", fap, "flipper-zero-wifi-dev-board"
        )

    def test_minimal_install_explicitly_skips_marauder_provisioning(self) -> None:
        module = load_device_install_module()
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "update.fuf"
            manifest.write_text("fixture", encoding="utf-8")
            with mock.patch.object(module, "install", return_value=0), mock.patch.object(
                module, "provision_marauder"
            ) as provision:
                result = module.main(["--skip-marauder", str(manifest)])

        self.assertEqual(result, 0)
        provision.assert_not_called()

    def test_flash_target_runs_live_preflight_before_constructing_install(self) -> None:
        source = (REPOSITORY_ROOT / "SConstruct").read_text()

        preflight = source.index("distenv.UsbPreflight()")
        install = source.index("distenv.AddUsbFlashTarget(")
        self.assertLess(preflight, install)

    def test_fbt_exposes_shared_exact_device_doctor(self) -> None:
        source = (REPOSITORY_ROOT / "SConstruct").read_text()
        self.assertIn('"device_doctor"', source)
        self.assertIn('distenv.Alias("doctor", device_doctor)', source)
        self.assertIn('"${FBT_SCRIPT_DIR}/device_install.py"', source)
        self.assertIn('"--doctor"', source)

    def test_dfu_parser_ignores_displaylink_and_deduplicates_flipper_alts(self) -> None:
        module = load_device_install_module()
        output = """
Found Runtime: [17e9:6000] serial="SUBO360100012"
Found DFU: [0483:df11] alt=2 serial="2075308D4242"
Found DFU: [0483:df11] alt=1 serial="2075308D4242"
Found DFU: [0483:df11] alt=0 serial="2075308D4242"
"""

        self.assertEqual(
            module.parse_flipper_dfu_serials(output), ["2075308D4242"]
        )

    def test_recovery_accepts_late_qflipper_error_only_after_live_rpc_identity(self) -> None:
        module = load_device_install_module()
        device_info = {
            "hardware_model": "Flipper Zero",
            "hardware_target": "7",
            "firmware_version": "1.4.3",
        }

        with mock.patch.object(
            module, "run_qflipper_repair", return_value=1
        ) as repair, mock.patch.object(
            module,
            "wait_for_flipper_runtime",
            return_value="/dev/cu.usbmodemflip_Osprit1",
        ) as wait_runtime, mock.patch.object(
            module, "_probe_rpc", return_value=((0, 15), device_info)
        ) as probe:
            recovered_port = module.recover_flipper_dfu(
                "2075308D4242", expected_runtime_serial="flip_Osprit"
            )

        self.assertEqual(recovered_port, "/dev/cu.usbmodemflip_Osprit1")
        repair.assert_called_once_with("2075308D4242")
        wait_runtime.assert_called_once_with("flip_Osprit")
        probe.assert_called_once_with("/dev/cu.usbmodemflip_Osprit1")

    def test_recovery_rejects_qflipper_error_when_runtime_does_not_return(self) -> None:
        module = load_device_install_module()

        with mock.patch.object(
            module, "run_qflipper_repair", return_value=1
        ), mock.patch.object(
            module, "wait_for_flipper_runtime", return_value=None
        ), mock.patch.object(module, "_probe_rpc") as probe:
            with self.assertRaisesRegex(
                RuntimeError, "qFlipper exited with status 1.*runtime did not return"
            ):
                module.recover_flipper_dfu("2075308D4242")

        probe.assert_not_called()

    def test_recovery_rejects_runtime_that_fails_flipper_rpc_identity(self) -> None:
        module = load_device_install_module()

        with mock.patch.object(
            module, "run_qflipper_repair", return_value=0
        ), mock.patch.object(
            module,
            "wait_for_flipper_runtime",
            return_value="/dev/cu.usbmodemflip_Osprit1",
        ), mock.patch.object(
            module, "_probe_rpc", side_effect=module.RpcError("wrong hardware target")
        ):
            with self.assertRaisesRegex(
                RuntimeError, "returned after recovery but failed RPC identity"
            ):
                module.recover_flipper_dfu("2075308D4242")

    def test_normal_pipeline_uses_protobuf_rpc_without_repair(self) -> None:
        module = load_device_install_module()
        manifest = mock.Mock()
        manifest.is_file.return_value = True

        with mock.patch.object(module, "detect_flipper_dfu", return_value=None), mock.patch.object(
            module, "run_rpc_update", return_value=0
        ) as rpc_update, mock.patch.object(
            module, "verify_poisoned_os", return_value=True
        ) as verify, mock.patch.object(module, "run_qflipper_repair") as repair:
            result = module.install("/dev/fixture", manifest, ["--fixture"])

        self.assertEqual(result, 0)
        rpc_update.assert_called_once_with(
            "/dev/fixture", manifest, ["--fixture"]
        )
        verify.assert_called_once_with("/dev/fixture")
        repair.assert_not_called()

    def test_dfu_pipeline_repairs_then_installs_requested_update(self) -> None:
        module = load_device_install_module()
        manifest = mock.Mock()
        manifest.is_file.return_value = True

        with mock.patch.object(
            module, "detect_flipper_dfu", return_value="2075308D4242"
        ), mock.patch.object(
            module,
            "recover_flipper_dfu",
            return_value="/dev/cu.usbmodemflip_Osprit1",
        ) as recover, mock.patch.object(
            module, "run_rpc_update", return_value=0
        ) as rpc_update, mock.patch.object(
            module, "verify_poisoned_os", return_value=True
        ) as verify:
            result = module.install("auto", manifest, [])

        self.assertEqual(result, 0)
        recover.assert_called_once_with("2075308D4242")
        rpc_update.assert_called_once_with(
            "/dev/cu.usbmodemflip_Osprit1", manifest, []
        )
        verify.assert_called_once_with("/dev/cu.usbmodemflip_Osprit1")

    def test_failed_selfupdate_recovers_only_when_flipper_enters_dfu(self) -> None:
        module = load_device_install_module()
        manifest = mock.Mock()
        manifest.is_file.return_value = True

        with mock.patch.object(
            module,
            "detect_flipper_dfu",
            side_effect=[None, "2075308D4242"],
        ), mock.patch.object(
            module,
            "recover_flipper_dfu",
            return_value="/dev/cu.usbmodemflip_Osprit1",
        ) as recover, mock.patch.object(
            module, "run_rpc_update", side_effect=[5, 0]
        ) as rpc_update, mock.patch.object(
            module, "verify_poisoned_os", return_value=True
        ) as verify:
            result = module.install("auto", manifest, [])

        self.assertEqual(result, 0)
        recover.assert_called_once_with("2075308D4242")
        self.assertEqual(rpc_update.call_count, 2)
        self.assertEqual(
            rpc_update.call_args_list,
            [
                mock.call("auto", manifest, []),
                mock.call("/dev/cu.usbmodemflip_Osprit1", manifest, []),
            ],
        )
        verify.assert_called_once_with("/dev/cu.usbmodemflip_Osprit1")

    def test_failed_runtime_never_invokes_qflipper_without_exact_dfu(self) -> None:
        module = load_device_install_module()
        manifest = mock.Mock()
        manifest.is_file.return_value = True

        with mock.patch.object(
            module, "detect_flipper_dfu", return_value=None
        ), mock.patch.object(
            module,
            "detect_flipper_runtime",
            return_value=("/dev/cu.usbmodemflip_Osprit1", "flip_Osprit"),
        ) as detect_runtime, mock.patch.object(
            module, "wait_for_flipper_dfu", return_value=None
        ) as wait_dfu, mock.patch.object(
            module, "run_qflipper_repair"
        ) as repair, mock.patch.object(
            module, "run_rpc_update", return_value=5
        ) as rpc_update, mock.patch.object(
            module, "verify_poisoned_os"
        ) as verify:
            result = module.install(
                "/dev/cu.usbmodemflip_Osprit1", manifest, []
            )

        self.assertEqual(result, 3)
        self.assertEqual(
            detect_runtime.call_args_list,
            [
                mock.call("/dev/cu.usbmodemflip_Osprit1"),
                mock.call("/dev/cu.usbmodemflip_Osprit1"),
            ],
        )
        wait_dfu.assert_called_once_with()
        repair.assert_not_called()
        rpc_update.assert_called_once_with(
            "/dev/cu.usbmodemflip_Osprit1", manifest, []
        )
        verify.assert_not_called()

    def test_failed_runtime_rpc_waits_for_dfu_then_recovers_and_retries(self) -> None:
        module = load_device_install_module()
        manifest = mock.Mock()
        manifest.is_file.return_value = True

        with mock.patch.object(
            module, "detect_flipper_dfu", return_value=None
        ), mock.patch.object(
            module,
            "detect_flipper_runtime",
            return_value=("/dev/cu.usbmodemflip_Osprit1", "flip_Osprit"),
        ), mock.patch.object(
            module, "wait_for_flipper_dfu", return_value="2075308D4242"
        ) as wait_dfu, mock.patch.object(
            module,
            "recover_flipper_dfu",
            return_value="/dev/cu.usbmodemflip_Osprit1",
        ) as recover, mock.patch.object(
            module, "run_rpc_update", side_effect=[5, 0]
        ) as rpc_update, mock.patch.object(
            module, "verify_poisoned_os", return_value=True
        ):
            result = module.install(
                "/dev/cu.usbmodemflip_Osprit1", manifest, []
            )

        self.assertEqual(result, 0)
        wait_dfu.assert_called_once_with()
        recover.assert_called_once_with(
            "2075308D4242", expected_runtime_serial="flip_Osprit"
        )
        self.assertEqual(rpc_update.call_count, 2)

    def test_successful_update_timeout_recovers_device_without_retrying_update(self) -> None:
        module = load_device_install_module()
        manifest = mock.Mock()
        manifest.is_file.return_value = True

        with mock.patch.object(
            module, "detect_flipper_dfu", return_value=None
        ), mock.patch.object(
            module,
            "detect_flipper_runtime",
            return_value=("/dev/cu.fixture", "flip_fixture"),
        ), mock.patch.object(
            module, "run_rpc_update", return_value=0
        ), mock.patch.object(
            module, "verify_poisoned_os", return_value=False
        ), mock.patch.object(
            module,
            "recover_after_failed_update",
            return_value="/dev/cu.recovered",
        ) as recover:
            result = module.install("auto", manifest, [])

        self.assertEqual(result, 6)
        recover.assert_called_once_with("flip_fixture")

    def test_post_install_verification_allows_full_update_and_unlock_window(self) -> None:
        module = load_device_install_module()

        self.assertGreaterEqual(module.POST_INSTALL_TIMEOUT, 600.0)

    def test_post_install_verification_bounds_rpc_attempt_by_remaining_time(self) -> None:
        module = load_device_install_module()
        clock = [0.0]

        def monotonic() -> float:
            return clock[0]

        def sleep(delay: float) -> None:
            clock[0] += delay

        rpc = mock.MagicMock()
        rpc.__enter__.side_effect = module.RpcError("fixture timeout")
        with mock.patch.object(
            module,
            "detect_flipper_runtime",
            return_value=("/dev/cu.fixture", "flip_fixture"),
        ), mock.patch.object(
            module, "FlipperRpc", return_value=rpc
        ) as rpc_class, mock.patch.object(
            module.time, "monotonic", side_effect=monotonic
        ), mock.patch.object(
            module.time, "sleep", side_effect=sleep
        ), mock.patch("sys.stderr", new_callable=io.StringIO):
            result = module.verify_poisoned_os("/dev/cu.fixture", timeout=3.0)

        self.assertFalse(result)
        self.assertGreater(rpc_class.call_count, 0)
        for call in rpc_class.call_args_list:
            self.assertGreater(call.kwargs["timeout"], 0.0)
            self.assertLessEqual(call.kwargs["timeout"], 3.0)

    def test_post_install_verification_explains_locked_runtime(self) -> None:
        module = load_device_install_module()
        clock = [0.0]

        def monotonic() -> float:
            return clock[0]

        def sleep(delay: float) -> None:
            clock[0] += delay

        rpc = mock.MagicMock()
        rpc.__enter__.side_effect = module.RpcError(
            "timed out waiting for Flipper CLI marker b'>: '"
        )
        stderr = io.StringIO()
        with mock.patch.object(
            module,
            "detect_flipper_runtime",
            return_value=("/dev/cu.fixture", "flip_fixture"),
        ), mock.patch.object(
            module, "FlipperRpc", return_value=rpc
        ), mock.patch.object(
            module.time, "monotonic", side_effect=monotonic
        ), mock.patch.object(
            module.time, "sleep", side_effect=sleep
        ), mock.patch("sys.stderr", stderr):
            result = module.verify_poisoned_os("/dev/cu.fixture", timeout=3.0)

        self.assertFalse(result)
        self.assertIn("unlock the device", stderr.getvalue())

    def test_failed_identity_check_enters_diagnosis_but_never_retries_update(self) -> None:
        module = load_device_install_module()
        manifest = mock.Mock()
        manifest.is_file.return_value = True

        with mock.patch.object(
            module, "detect_flipper_dfu", return_value=None
        ), mock.patch.object(
            module,
            "detect_flipper_runtime",
            return_value=("/dev/cu.fixture", "flip_fixture"),
        ), mock.patch.object(
            module, "run_rpc_update", return_value=0
        ) as rpc_update, mock.patch.object(
            module, "verify_poisoned_os", return_value=False
        ) as verify, mock.patch.object(
            module,
            "recover_after_failed_update",
            return_value="/dev/cu.fixture",
        ) as recover:
            result = module.install("auto", manifest, [])

        self.assertEqual(result, 6)
        rpc_update.assert_called_once_with("auto", manifest, [])
        verify.assert_called_once_with("auto")
        recover.assert_called_once_with("flip_fixture")

    def test_doctor_repairs_exact_dfu_then_collects_runtime_diagnostics(self) -> None:
        module = load_device_install_module()
        with mock.patch.object(
            module, "detect_flipper_dfu", return_value="2075308D4242"
        ), mock.patch.object(
            module,
            "recover_flipper_dfu",
            return_value="/dev/cu.usbmodemflip_Osprit1",
        ) as recover, mock.patch.object(
            module, "collect_runtime_diagnostics", return_value=True
        ) as diagnose:
            result = module.doctor("auto", recover=True)

        self.assertEqual(result, 0)
        recover.assert_called_once_with("2075308D4242")
        diagnose.assert_called_once_with("/dev/cu.usbmodemflip_Osprit1")

    def test_doctor_waits_for_exact_dfu_when_device_is_absent(self) -> None:
        module = load_device_install_module()
        with mock.patch.object(
            module, "detect_flipper_dfu", return_value=None
        ), mock.patch.object(
            module, "detect_flipper_runtime", return_value=None
        ), mock.patch.object(
            module, "wait_for_recovery_device", return_value=("dfu", "2075308D4242")
        ) as wait, mock.patch.object(
            module,
            "recover_flipper_dfu",
            return_value="/dev/cu.usbmodemflip_Osprit1",
        ) as recover, mock.patch.object(
            module, "collect_runtime_diagnostics", return_value=True
        ):
            result = module.doctor("auto", recover=True)

        self.assertEqual(result, 0)
        wait.assert_called_once_with(None)
        recover.assert_called_once_with("2075308D4242")

    def test_runtime_diagnostics_report_storage_and_update_milestones(self) -> None:
        module = load_device_install_module()
        rpc = mock.MagicMock()
        rpc.__enter__.return_value = rpc
        rpc.protobuf_version.return_value = (0, 25)
        rpc.device_info.return_value = {
            "hardware_model": "Flipper Zero",
            "hardware_target": "7",
            "firmware_version": "1.4.3",
            "firmware_origin_fork": "PoisonedOS",
        }
        rpc.storage_info.return_value = {
            "total_space": 64 * 1024 * 1024,
            "free_space": 8 * 1024 * 1024,
        }
        rpc.list_dir.return_value = [
            {"type": "file", "name": "backup.tar", "size": 11_264},
            {"type": "file", "name": "resources.ths", "size": 7_766_813},
        ]
        rpc.stat.side_effect = lambda path: (
            {"type": "file", "name": Path(path).name, "size": 1_055_104}
            if path.endswith("esp32_marauder.flipper.bin")
            else None
        )
        output = io.StringIO()

        with mock.patch.object(module, "FlipperRpc", return_value=rpc), mock.patch(
            "sys.stdout", output
        ):
            result = module.collect_runtime_diagnostics("/dev/cu.fixture")

        self.assertTrue(result)
        self.assertIn("firmware_origin_fork=PoisonedOS", output.getvalue())
        self.assertIn("free=8388608", output.getvalue())
        self.assertIn("backup.tar", output.getvalue())
        self.assertIn("marauder-s2=present", output.getvalue())

    def test_preflight_proves_rpc_and_exact_flipper_identity(self) -> None:
        module = load_device_install_module()
        rpc = mock.MagicMock()
        rpc.__enter__.return_value = rpc
        rpc.protobuf_version.return_value = (88, 42)
        rpc.device_info.return_value = {
            "hardware_model": "Flipper Zero",
            "hardware_target": "7",
            "firmware_version": "1.4.3",
        }

        with mock.patch.object(
            module,
            "detect_flipper_runtime",
            return_value=("/dev/cu.fixture", "flip_fixture"),
        ), mock.patch.object(
            module, "FlipperRpc", return_value=rpc
        ) as rpc_class:
            result = module.preflight("auto")

        self.assertEqual(result, 0)
        rpc_class.assert_called_once_with("/dev/cu.fixture")
        rpc.protobuf_version.assert_called_once_with()
        rpc.device_info.assert_called_once_with()

    def test_preflight_fails_before_build_when_rpc_is_unresponsive(self) -> None:
        module = load_device_install_module()
        rpc = mock.MagicMock()
        rpc.__enter__.side_effect = module.RpcError("fixture timeout")

        with mock.patch.object(
            module,
            "detect_flipper_runtime",
            return_value=("/dev/cu.fixture", "flip_fixture"),
        ), mock.patch.object(
            module, "FlipperRpc", return_value=rpc
        ), mock.patch.object(
            module, "run_qflipper_runtime_repair", return_value=None
        ), mock.patch.object(module, "detect_flipper_dfu", return_value=None):
            result = module.preflight("auto")

        self.assertEqual(result, 4)

    def test_preflight_never_auto_repairs_an_unresponsive_runtime(self) -> None:
        module = load_device_install_module()
        failed_rpc = mock.MagicMock()
        failed_rpc.__enter__.side_effect = module.RpcError("fixture timeout")

        with mock.patch.object(
            module, "detect_flipper_dfu", return_value=None
        ), mock.patch.object(
            module,
            "detect_flipper_runtime",
            return_value=("/dev/cu.fixture", "flip_fixture"),
        ), mock.patch.object(
            module,
            "FlipperRpc",
            return_value=failed_rpc,
        ), mock.patch.object(
            module,
            "run_qflipper_runtime_repair",
            return_value="/dev/cu.fixture",
        ) as repair:
            result = module.preflight("auto")

        self.assertEqual(result, 4)
        repair.assert_not_called()

    def test_device_info_parser_reads_cli_properties(self) -> None:
        module = load_device_install_module()

        self.assertEqual(
            module.parse_device_info(
                b"hardware_model : Flipper Zero\r\n"
                b"firmware_origin_fork : PoisonedOS\r\n"
            ),
            {
                "hardware_model": "Flipper Zero",
                "firmware_origin_fork": "PoisonedOS",
            },
        )


class BaselineProbeTests(unittest.TestCase):
    def test_parses_and_validates_poisonedos_device_and_sd_fixture(self) -> None:
        module = load_testops_module()
        sentinel = b'{"fixture":"test"}\n'
        storage = mock.Mock()
        storage.CLI_PROMPT = ">: "
        storage.read.until.return_value = (
            b"hardware_model : Flipper Zero\r\n"
            b"hardware_target : 7\r\n"
            b"firmware_version : poisonedos\r\n"
            b"firmware_origin_fork : PoisonedOS\r\n"
        )
        storage.read_file.return_value = sentinel

        evidence = module.collect_baseline_probe(
            storage,
            role="test",
            sentinel_path="/ext/.poison-hil/test.json",
            sentinel_sha256=hashlib.sha256(sentinel).hexdigest(),
        )

        self.assertEqual(evidence["deviceInfo"]["hardware_model"], "Flipper Zero")
        self.assertEqual(evidence["deviceInfo"]["firmware_origin_fork"], "PoisonedOS")
        self.assertEqual(evidence["sdFixture"]["bytes"], len(sentinel))
        storage.send_and_wait_eol.assert_called_once_with("device_info\r")

    def test_rejects_wrong_firmware_origin_or_sd_fixture(self) -> None:
        module = load_testops_module()
        storage = mock.Mock()
        storage.CLI_PROMPT = ">: "
        storage.read.until.return_value = (
            b"hardware_model : Flipper Zero\r\n"
            b"hardware_target : 7\r\n"
            b"firmware_origin_fork : Official\r\n"
        )
        storage.read_file.return_value = b"wrong"

        with self.assertRaisesRegex(ValueError, "firmware_origin_fork"):
            module.collect_baseline_probe(
                storage,
                role="test",
                sentinel_path="/ext/.poison-hil/test.json",
                sentinel_sha256="0" * 64,
            )

    def test_parses_runtime_thread_and_peak_heap_metrics(self) -> None:
        module = load_testops_module()
        storage = mock.Mock()
        storage.CLI_PROMPT = ">: "
        storage.read.until.return_value = (
            b"Threads: 23, ISR Time: 0.18%, Uptime: 0h1m2s\r\n"
            b"Heap: total 191232, free 112000, minimum 105500, max block 90000\r\n"
        )

        metrics = module.collect_runtime_probe(storage)

        self.assertEqual(metrics["threads"], 23)
        self.assertEqual(metrics["heapTotalBytes"], 191232)
        self.assertEqual(metrics["heapMinimumFreeBytes"], 105500)
        self.assertEqual(metrics["peakHeapUsedBytes"], 85732)
        storage.send_and_wait_eol.assert_called_once_with("top 0\r")

    def test_rejects_incomplete_runtime_output(self) -> None:
        module = load_testops_module()
        storage = mock.Mock()
        storage.CLI_PROMPT = ">: "
        storage.read.until.return_value = b"Threads: 23\r\n"

        with self.assertRaisesRegex(ValueError, "runtime metrics"):
            module.collect_runtime_probe(storage)


if __name__ == "__main__":
    unittest.main()
