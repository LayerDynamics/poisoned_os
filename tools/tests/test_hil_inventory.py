from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
RUNNER_PATH = REPOSITORY_ROOT / "tools" / "hil" / "run_suite.py"
EXAMPLE_PATH = REPOSITORY_ROOT / "tools" / "hil" / "inventory.example.json"


def load_runner_module():
    spec = importlib.util.spec_from_file_location("poison_hil_runner", RUNNER_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {RUNNER_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def valid_inventory() -> dict:
    return {
        "schema": "poison.hil.inventory/v1",
        "devices": [
            {
                "role": "test",
                "serial": "flip_TestFixture123",
                "usbPower": {
                    "driver": "uhubctl",
                    "location": "1-2.3",
                    "port": 1,
                },
                "sdFixture": {
                    "sentinelPath": "/ext/.poison-hil/test.json",
                    "sha256": "1" * 64,
                },
            },
            {
                "role": "recovery",
                "serial": "flip_RecoveryFixture456",
                "usbPower": {
                    "driver": "uhubctl",
                    "location": "1-2.4",
                    "port": 2,
                },
                "sdFixture": {
                    "sentinelPath": "/ext/.poison-hil/recovery.json",
                    "sha256": "2" * 64,
                },
                "recovery": {
                    "driver": "fbt-swd",
                    "transport": "blackmagic_usb",
                    "probeSerial": "BMP-01234567",
                    "dfuSerial": "206E366D5748",
                },
            },
        ],
    }


class HilInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.runner = load_runner_module()

    def test_committed_example_is_valid_only_as_a_template(self) -> None:
        example = self.runner.load_inventory(EXAMPLE_PATH, allow_placeholders=True)
        self.assertEqual(set(example), {"test", "recovery"})
        with self.assertRaisesRegex(self.runner.InventoryError, "placeholder"):
            self.runner.load_inventory(EXAMPLE_PATH)

    def test_live_inventory_requires_exactly_two_named_roles(self) -> None:
        inventory = valid_inventory()
        inventory["devices"].pop()
        with self.assertRaisesRegex(self.runner.InventoryError, "exactly"):
            self.runner.validate_inventory(inventory)

    def test_live_inventory_rejects_duplicate_device_serials(self) -> None:
        inventory = valid_inventory()
        inventory["devices"][1]["serial"] = inventory["devices"][0]["serial"]
        with self.assertRaisesRegex(self.runner.InventoryError, "unique"):
            self.runner.validate_inventory(inventory)

    def test_live_inventory_requires_power_sd_and_recovery_control(self) -> None:
        cases = (
            ("usbPower", "usbPower"),
            ("sdFixture", "sdFixture"),
            ("recovery", "recovery"),
        )
        for field, expected in cases:
            with self.subTest(field=field):
                inventory = valid_inventory()
                target = inventory["devices"][1]
                if field != "recovery":
                    target = inventory["devices"][0]
                del target[field]
                with self.assertRaisesRegex(self.runner.InventoryError, expected):
                    self.runner.validate_inventory(inventory)

    def test_live_inventory_rejects_credentials_and_unknown_fields(self) -> None:
        inventory = valid_inventory()
        inventory["devices"][0]["password"] = "do-not-store-this"
        with self.assertRaisesRegex(self.runner.InventoryError, "unknown field"):
            self.runner.validate_inventory(inventory)

    def test_live_inventory_rejects_unsafe_sd_paths_and_invalid_digests(self) -> None:
        inventory = valid_inventory()
        inventory["devices"][0]["sdFixture"] = {
            "sentinelPath": "/int/secret",
            "sha256": "not-a-digest",
        }
        with self.assertRaisesRegex(self.runner.InventoryError, "sentinelPath"):
            self.runner.validate_inventory(inventory)

    def test_load_inventory_rejects_duplicate_json_keys(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "inventory.json"
            path.write_text(
                '{"schema":"poison.hil.inventory/v1",'
                '"schema":"poison.hil.inventory/v2","devices":[]}',
                encoding="utf-8",
            )
            with self.assertRaisesRegex(self.runner.InventoryError, "duplicate"):
                self.runner.load_inventory(path)

    def test_valid_inventory_is_indexed_by_role_without_secrets(self) -> None:
        inventory = self.runner.validate_inventory(valid_inventory())
        self.assertEqual(inventory["test"].serial, "flip_TestFixture123")
        self.assertEqual(inventory["recovery"].recovery.probe_serial, "BMP-01234567")

    def test_json_round_trip_is_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "inventory.json"
            path.write_text(json.dumps(valid_inventory()), encoding="utf-8")
            inventory = self.runner.load_inventory(path)
        self.assertEqual(tuple(inventory), ("test", "recovery"))

    def test_port_discovery_requires_flipper_ids_and_exact_serial(self) -> None:
        ports = self.runner.SerialEnumeration(
            "test-provider",
            (
                self.runner.SerialPortRecord(
                    "/dev/cu.displaylink", "flip_TestFixture123", 0x17E9, 0x6000
                ),
                self.runner.SerialPortRecord(
                    "/dev/cu.wrong-product", "flip_TestFixture123", 0x0483, 0xDF11
                ),
                self.runner.SerialPortRecord(
                    "/dev/cu.usbmodemflip_TestFixture123",
                    "flip_TestFixture123",
                    0x0483,
                    0x5740,
                ),
            ),
        )
        context = self.runner.ExecutionContext(
            Path("unused"), 1, serial_port_provider=lambda: ports
        )

        resolved = context.discover_ports({"test": self.runner.validate_inventory(valid_inventory())["test"]})

        self.assertEqual(
            resolved, {"test": "/dev/cu.usbmodemflip_TestFixture123"}
        )
        self.assertEqual(context.observations["serialEnumerationSource"], "test-provider")

    def test_native_serial_inventory_parser_preserves_usb_identity(self) -> None:
        ports = self.runner._parse_native_serial_inventory(
            json.dumps(
                {
                    "schema": "poison.usb-serial-ports/v1",
                    "ports": [
                        {
                            "device": "COM9",
                            "serialNumber": "flip_TestFixture123",
                            "vendorId": 0x0483,
                            "productId": 0x5740,
                        }
                    ],
                }
            )
        )

        self.assertEqual(
            ports,
            (
                self.runner.SerialPortRecord(
                    "COM9", "flip_TestFixture123", 0x0483, 0x5740
                ),
            ),
        )

    def test_native_serial_inventory_parser_rejects_ambiguous_fields(self) -> None:
        with self.assertRaisesRegex(self.runner.HilError, "invalid fields"):
            self.runner._parse_native_serial_inventory(
                json.dumps(
                    {
                        "schema": "poison.usb-serial-ports/v1",
                        "ports": [
                            {
                                "device": "COM9",
                                "serialNumber": "flip_TestFixture123",
                                "vendorId": 0x0483,
                                "productId": 0x5740,
                                "label": "trust me",
                            }
                        ],
                    }
                )
            )

    def test_explicit_hil_python_must_be_an_executable_file(self) -> None:
        interpreter = Path(self.runner.sys.executable).resolve()
        with mock.patch.dict(
            "os.environ", {"POISON_HIL_PYTHON": str(interpreter)}, clear=False
        ):
            self.assertEqual(self.runner._pinned_fbt_python(), interpreter)

        with tempfile.TemporaryDirectory() as directory:
            with mock.patch.dict(
                "os.environ", {"POISON_HIL_PYTHON": directory}, clear=False
            ):
                with self.assertRaisesRegex(self.runner.HilError, "executable"):
                    self.runner._pinned_fbt_python()


if __name__ == "__main__":
    unittest.main()
