from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "verify_supported_clients.py"


def load_module():
    spec = importlib.util.spec_from_file_location("verify_supported_clients", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {MODULE_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class SupportedClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.module = load_module()

    def config(self) -> dict:
        return self.module.load_config(ROOT / "config/supported-clients.json")

    def test_repository_matrix_has_explicit_rows_and_routes(self) -> None:
        config = self.config()
        clients = self.module.validate_config(config)
        self.assertEqual(len(clients), 7)
        self.assertEqual(config["defaultRoute"], "web-runtime")
        self.assertTrue(any(client["browser"] == "Safari" for client in clients))
        self.assertTrue(all(client["routes"] == ["web-runtime"] for client in clients))
        self.assertTrue(all(client["status"] != "supported" for client in clients))

    def test_dashboard_entry_point_is_wifi_only(self) -> None:
        source = (ROOT / "dashboard" / "src" / "app" / "App.tsx").read_text(encoding="utf-8")
        self.assertIn("WebRuntimeTransport", source)
        self.assertIn("window.isSecureContext", source)
        for forbidden in (
            "WebSerialTransport",
            "WebBluetoothTransport",
            "BridgeWebSocketTransport",
            "Connect USB",
            "Connect Bluetooth",
        ):
            self.assertNotIn(forbidden, source)

        for relative_path in (
            "dashboard/e2e/javascript-workflow.spec.ts",
            "dashboard/e2e/javascript-sandbox.spec.ts",
        ):
            physical_source = (ROOT / relative_path).read_text(encoding="utf-8")
            self.assertIn("POISON_HIL_WIFI_BOARD_ID", physical_source)
            self.assertNotIn("POISON_HIL_BRIDGE_TOKEN", physical_source)

    def test_supported_row_requires_physical_evidence(self) -> None:
        config = self.config()
        config["clients"][0]["status"] = "supported"
        with self.assertRaisesRegex(self.module.SupportedClientError, "lacks hardwareEvidence"):
            self.module.validate_config(config)

    def test_supported_row_requires_evidence_route_in_matrix(self) -> None:
        config = self.config()
        config["clients"][0]["status"] = "supported"
        config["clients"][0]["hardwareEvidence"] = {
            "hostId": "mac-host-01",
            "deviceSerial": "flip-real-01",
            "route": "invalid-route",
            "command": "python3 tools/hil/run_suite.py --suite pair-control",
            "digest": "a" * 64,
        }
        with self.assertRaisesRegex(self.module.SupportedClientError, "evidence route"):
            self.module.validate_config(config)

    def test_supported_row_rejects_placeholder_evidence(self) -> None:
        config = self.config()
        config["clients"][0]["status"] = "supported"
        config["clients"][0]["hardwareEvidence"] = {
            "hostId": "example-host",
            "deviceSerial": "flip-real-01",
            "route": "web-runtime",
            "command": "python3 tools/hil/run_suite.py --suite pair-control",
            "digest": "a" * 64,
        }
        with self.assertRaisesRegex(self.module.SupportedClientError, "placeholder"):
            self.module.validate_config(config)

    def test_duplicate_ids_are_rejected(self) -> None:
        config = self.config()
        config["clients"].append(dict(config["clients"][0]))
        with self.assertRaisesRegex(self.module.SupportedClientError, "duplicate client id"):
            self.module.validate_config(config)

    def test_json_round_trip(self) -> None:
        config = self.config()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "clients.json"
            path.write_text(json.dumps(config), encoding="utf-8")
            self.assertEqual(
                self.module.validate_config(self.module.load_config(path))[0]["id"],
                "macos-chrome",
            )


if __name__ == "__main__":
    unittest.main()
