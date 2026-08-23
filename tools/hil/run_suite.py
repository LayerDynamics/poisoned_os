#!/usr/bin/env python3
"""Validate the private HIL inventory and execute a physical-device suite."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import subprocess
import sys
import time
from typing import Any, Callable, NamedTuple


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_INVENTORY = Path(__file__).with_name("inventory.json")
DEFAULT_RESULTS = Path(__file__).with_name("results")
SCHEMA = "poison.hil.inventory/v1"
ROLES = ("test", "recovery")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
FLIPPER_RUNTIME_VENDOR_ID = 0x0483
FLIPPER_RUNTIME_PRODUCT_ID = 0x5740
USB_SERIAL_SCHEMA = "poison.usb-serial-ports/v1"


class InventoryError(ValueError):
    """The HIL inventory is unsafe, incomplete, or ambiguous."""


class HilError(RuntimeError):
    """A physical HIL operation failed."""


class UsbPower(NamedTuple):
    location: str
    port: int
    settle_seconds: float


class SdFixture(NamedTuple):
    sentinel_path: str
    sha256: str


class RecoveryControl(NamedTuple):
    transport: str
    probe_serial: str
    dfu_serial: str


class Device(NamedTuple):
    role: str
    serial: str
    usb_power: UsbPower
    sd_fixture: SdFixture
    recovery: RecoveryControl | None


class SerialPortRecord(NamedTuple):
    device: str
    serial_number: str | None
    vendor_id: int | None
    product_id: int | None


class SerialEnumeration(NamedTuple):
    source: str
    ports: tuple[SerialPortRecord, ...]


SerialPortProvider = Callable[[], SerialEnumeration]


def _pyserial_site_packages() -> tuple[Path, ...]:
    toolchain = REPOSITORY_ROOT / "toolchain"
    candidates = {
        path.resolve()
        for pattern in ("*/lib/python*/site-packages", "*/Lib/site-packages")
        for path in toolchain.glob(pattern)
        if path.is_dir()
    }
    return tuple(sorted(candidates))


def _enumerate_with_pyserial() -> SerialEnumeration | None:
    try:
        from serial.tools import list_ports

        source = "python-environment"
    except ImportError:
        list_ports = None
        source = "pinned-fbt-toolchain"
        for site_packages in _pyserial_site_packages():
            candidate = str(site_packages)
            if candidate not in sys.path:
                sys.path.append(candidate)
            try:
                from serial.tools import list_ports as bundled_list_ports

                list_ports = bundled_list_ports
                break
            except ImportError:
                continue
        if list_ports is None:
            return None

    return SerialEnumeration(
        source,
        tuple(
            SerialPortRecord(
                port.device,
                port.serial_number,
                port.vid,
                port.pid,
            )
            for port in list_ports.comports()
        ),
    )


def _parse_native_serial_inventory(output: str) -> tuple[SerialPortRecord, ...]:
    try:
        value = json.loads(output, object_pairs_hook=_reject_duplicate_keys)
    except (json.JSONDecodeError, InventoryError) as error:
        raise HilError(f"native USB serial probe returned invalid JSON: {error}") from error
    if not isinstance(value, dict) or set(value) != {"schema", "ports"}:
        raise HilError("native USB serial probe returned an invalid inventory object")
    if value["schema"] != USB_SERIAL_SCHEMA or not isinstance(value["ports"], list):
        raise HilError("native USB serial probe returned an unsupported inventory schema")

    ports: list[SerialPortRecord] = []
    for index, raw_port in enumerate(value["ports"]):
        if not isinstance(raw_port, dict) or set(raw_port) != {
            "device", "serialNumber", "vendorId", "productId"
        }:
            raise HilError(f"native USB serial probe port {index} has invalid fields")
        device = raw_port["device"]
        serial_number = raw_port["serialNumber"]
        vendor_id = raw_port["vendorId"]
        product_id = raw_port["productId"]
        if not isinstance(device, str) or not device or any(
            character in device for character in ("\r", "\n", "\x00")
        ):
            raise HilError(f"native USB serial probe port {index} has an invalid device")
        if serial_number is not None and (
            not isinstance(serial_number, str)
            or not serial_number
            or any(character in serial_number for character in ("\r", "\n", "\x00"))
        ):
            raise HilError(f"native USB serial probe port {index} has an invalid serial number")
        if any(
            isinstance(identifier, bool)
            or not isinstance(identifier, int)
            or identifier < 0
            or identifier > 0xFFFF
            for identifier in (vendor_id, product_id)
        ):
            raise HilError(f"native USB serial probe port {index} has invalid USB identifiers")
        ports.append(SerialPortRecord(device, serial_number, vendor_id, product_id))
    return tuple(ports)


def _native_serial_probe_command() -> list[str]:
    configured = os.environ.get("POISON_SERIAL_PROBE")
    if configured:
        candidate = Path(configured).expanduser()
        if not candidate.is_file() or not os.access(candidate, os.X_OK):
            raise HilError("POISON_SERIAL_PROBE must name an executable poisoned-bridge binary")
        return [str(candidate), "list-usb-serial-json"]

    cargo = shutil.which("cargo")
    if not cargo:
        raise HilError(
            "USB serial enumeration needs the pinned FBT toolchain pyserial or the Rust toolchain"
        )
    return [
        cargo,
        "run",
        "--quiet",
        "--manifest-path",
        str(REPOSITORY_ROOT / "bridge" / "Cargo.toml"),
        "--bin",
        "poisoned-bridge",
        "--",
        "list-usb-serial-json",
    ]


def enumerate_serial_ports() -> SerialEnumeration:
    python_enumeration = _enumerate_with_pyserial()
    if python_enumeration is not None:
        return python_enumeration

    command = _native_serial_probe_command()
    try:
        result = subprocess.run(
            command,
            cwd=REPOSITORY_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=180,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise HilError(f"native USB serial probe failed to execute: {error}") from error
    if result.returncode != 0:
        detail = result.stderr.strip() or f"exit code {result.returncode}"
        raise HilError(f"native USB serial probe failed: {detail}")
    return SerialEnumeration("rust-tokio-serial", _parse_native_serial_inventory(result.stdout))


def _pinned_fbt_python() -> Path:
    configured = os.environ.get("POISON_HIL_PYTHON")
    if configured:
        candidate = Path(configured).expanduser().resolve()
        if not candidate.is_file() or not os.access(candidate, os.X_OK):
            raise HilError("POISON_HIL_PYTHON must name an executable Python interpreter")
        return candidate

    machine = platform.machine().lower()
    machine = {"amd64": "x86_64", "aarch64": "arm64"}.get(machine, machine)
    system = platform.system().lower()
    toolchain = REPOSITORY_ROOT / "toolchain" / f"{machine}-{system}"
    candidates = (
        toolchain / "bin" / "python3",
        toolchain / "python" / "python.exe",
    )
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate.resolve()
    raise HilError(
        "the pinned FBT Python runtime is not installed; run ./fbt --help once before a physical HIL suite"
    )


def _reexec_with_pinned_fbt_python(arguments: list[str]) -> None:
    pinned_python = _pinned_fbt_python()
    try:
        current_python = Path(sys.executable).resolve()
    except OSError:
        current_python = Path(sys.executable)
    if current_python == pinned_python:
        return
    os.execve(
        pinned_python,
        [str(pinned_python), str(Path(__file__).resolve()), *arguments],
        os.environ.copy(),
    )


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise InventoryError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _object(value: Any, name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise InventoryError(f"{name} must be an object")
    return value


def _exact_fields(
    value: dict[str, Any], name: str, required: set[str], optional: set[str] = set()
) -> None:
    missing = required - value.keys()
    if missing:
        raise InventoryError(f"{name} missing field(s): {', '.join(sorted(missing))}")
    unknown = value.keys() - required - optional
    if unknown:
        raise InventoryError(
            f"{name} has unknown field(s): {', '.join(sorted(unknown))}"
        )


def _string(value: Any, name: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise InventoryError(f"{name} must be a non-empty string")
    if any(character in value for character in ("\r", "\n", "\x00")):
        raise InventoryError(f"{name} contains a forbidden control character")
    return value


def _is_placeholder(value: str) -> bool:
    upper = value.upper()
    return upper.startswith(("REPLACE_", "CHANGE_ME", "EXAMPLE_")) or (
        len(value) == 64 and set(value) == {"0"}
    )


def _validate_power(value: Any, name: str) -> UsbPower:
    power = _object(value, name)
    _exact_fields(power, name, {"driver", "location", "port"}, {"settleSeconds"})
    if power["driver"] != "uhubctl":
        raise InventoryError(f"{name}.driver must be uhubctl")
    location = _string(power["location"], f"{name}.location")
    port = power["port"]
    if isinstance(port, bool) or not isinstance(port, int) or port < 1:
        raise InventoryError(f"{name}.port must be a positive integer")
    settle = power.get("settleSeconds", 2.0)
    if isinstance(settle, bool) or not isinstance(settle, (int, float)) or settle < 0.5:
        raise InventoryError(f"{name}.settleSeconds must be at least 0.5")
    return UsbPower(location, port, float(settle))


def _validate_sd(value: Any, name: str) -> SdFixture:
    fixture = _object(value, name)
    _exact_fields(fixture, name, {"sentinelPath", "sha256"})
    sentinel_path = _string(fixture["sentinelPath"], f"{name}.sentinelPath")
    parsed_path = PurePosixPath(sentinel_path)
    if (
        not sentinel_path.startswith("/ext/")
        or ".." in parsed_path.parts
        or parsed_path.name in ("", ".", "..")
    ):
        raise InventoryError(f"{name}.sentinelPath must be a file below /ext")
    digest = _string(fixture["sha256"], f"{name}.sha256").lower()
    if not SHA256_PATTERN.fullmatch(digest):
        raise InventoryError(f"{name}.sha256 must be a lowercase SHA-256 digest")
    return SdFixture(sentinel_path, digest)


def _validate_recovery(value: Any, name: str) -> RecoveryControl:
    recovery = _object(value, name)
    _exact_fields(
        recovery,
        name,
        {"driver", "transport", "probeSerial", "dfuSerial"},
    )
    if recovery["driver"] != "fbt-swd":
        raise InventoryError(f"{name}.driver must be fbt-swd")
    transport = _string(recovery["transport"], f"{name}.transport")
    if transport not in {"blackmagic_usb", "stlink"}:
        raise InventoryError(f"{name}.transport must be blackmagic_usb or stlink")
    return RecoveryControl(
        transport,
        _string(recovery["probeSerial"], f"{name}.probeSerial"),
        _string(recovery["dfuSerial"], f"{name}.dfuSerial"),
    )


def validate_inventory(
    raw: Any, *, allow_placeholders: bool = False
) -> dict[str, Device]:
    root = _object(raw, "inventory")
    _exact_fields(root, "inventory", {"schema", "devices"})
    if root["schema"] != SCHEMA:
        raise InventoryError(f"inventory.schema must be {SCHEMA}")
    if not isinstance(root["devices"], list) or len(root["devices"]) != 2:
        raise InventoryError("inventory.devices must contain exactly two devices")

    devices: dict[str, Device] = {}
    serials: set[str] = set()
    for index, raw_device in enumerate(root["devices"]):
        name = f"inventory.devices[{index}]"
        item = _object(raw_device, name)
        _exact_fields(
            item,
            name,
            {"role", "serial", "usbPower", "sdFixture"},
            {"recovery"},
        )
        role = _string(item["role"], f"{name}.role")
        if role not in ROLES or role in devices:
            raise InventoryError(
                "device roles must be exactly one test and one recovery"
            )
        serial = _string(item["serial"], f"{name}.serial")
        if serial in serials:
            raise InventoryError("device serials must be unique")
        serials.add(serial)

        recovery = None
        if role == "recovery":
            if "recovery" not in item:
                raise InventoryError(
                    f"{name}.recovery is required for the recovery role"
                )
            recovery = _validate_recovery(item["recovery"], f"{name}.recovery")
        elif "recovery" in item:
            raise InventoryError(f"{name}.recovery is only valid for the recovery role")

        device = Device(
            role,
            serial,
            _validate_power(item["usbPower"], f"{name}.usbPower"),
            _validate_sd(item["sdFixture"], f"{name}.sdFixture"),
            recovery,
        )
        placeholder_values = [
            device.serial,
            device.usb_power.location,
            device.sd_fixture.sha256,
        ]
        if recovery:
            placeholder_values.extend((recovery.probe_serial, recovery.dfu_serial))
        if not allow_placeholders and any(
            _is_placeholder(v) for v in placeholder_values
        ):
            raise InventoryError(f"{name} contains an example placeholder")
        devices[role] = device

    if tuple(devices) != ROLES and set(devices) != set(ROLES):
        raise InventoryError("device roles must be exactly one test and one recovery")
    return {role: devices[role] for role in ROLES}


def load_inventory(
    path: Path | str, *, allow_placeholders: bool = False
) -> dict[str, Device]:
    inventory_path = Path(path)
    try:
        raw = json.loads(
            inventory_path.read_text(encoding="utf-8"),
            object_pairs_hook=_reject_duplicate_keys,
        )
    except InventoryError:
        raise
    except (OSError, json.JSONDecodeError) as error:
        raise InventoryError(
            f"cannot read inventory {inventory_path}: {error}"
        ) from error
    return validate_inventory(raw, allow_placeholders=allow_placeholders)


class ExecutionContext:
    def __init__(
        self,
        results_dir: Path,
        timeout: int,
        serial_port_provider: SerialPortProvider = enumerate_serial_ports,
    ) -> None:
        self.results_dir = results_dir
        self.timeout = timeout
        self.serial_port_provider = serial_port_provider
        self.started_at = time.time()
        self.commands: list[dict[str, Any]] = []
        self.observations: dict[str, Any] = {}

    def require_program(self, program: str) -> str:
        resolved = shutil.which(program)
        if not resolved:
            raise HilError(f"required HIL program is unavailable: {program}")
        return resolved

    def run(
        self,
        label: str,
        argv: list[str],
        *,
        env: dict[str, str] | None = None,
        timeout: int | None = None,
        check: bool = True,
    ) -> subprocess.CompletedProcess[str]:
        print(f"HIL {label}: {' '.join(argv)}", flush=True)
        started = time.monotonic()
        command_env = os.environ.copy()
        if env:
            command_env.update(env)
        try:
            result = subprocess.run(
                argv,
                cwd=REPOSITORY_ROOT,
                env=command_env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=timeout or self.timeout,
                check=False,
            )
        except subprocess.TimeoutExpired as error:
            output = error.stdout or ""
            if isinstance(output, bytes):
                output = output.decode("utf-8", errors="replace")
            self._record_command(label, argv, 124, output, started)
            raise HilError(f"{label} timed out") from error
        print(result.stdout, end="")
        self._record_command(label, argv, result.returncode, result.stdout, started)
        if check and result.returncode != 0:
            raise HilError(f"{label} failed with exit code {result.returncode}")
        return result

    def _record_command(
        self, label: str, argv: list[str], returncode: int, output: str, started: float
    ) -> None:
        self.commands.append(
            {
                "label": label,
                "argv": argv,
                "returnCode": returncode,
                "elapsedSeconds": round(time.monotonic() - started, 3),
                "outputSha256": hashlib.sha256(output.encode("utf-8")).hexdigest(),
            }
        )

    def _flipper_ports(self) -> tuple[SerialPortRecord, ...]:
        enumeration = self.serial_port_provider()
        self.observations["serialEnumerationSource"] = enumeration.source
        return tuple(
            port
            for port in enumeration.ports
            if port.vendor_id == FLIPPER_RUNTIME_VENDOR_ID
            and port.product_id == FLIPPER_RUNTIME_PRODUCT_ID
            and port.serial_number is not None
            and port.serial_number.startswith("flip_")
        )

    def discover_ports(self, devices: dict[str, Device]) -> dict[str, str]:
        attached = self._flipper_ports()
        resolved: dict[str, str] = {}
        for role, device in devices.items():
            matches = [
                port.device for port in attached if port.serial_number == device.serial
            ]
            if len(matches) != 1:
                raise HilError(
                    f"role {role} expected serial {device.serial!r}, found {len(matches)} normal-mode ports"
                )
            resolved[role] = matches[0]
        if len(set(resolved.values())) != len(resolved):
            raise HilError("HIL roles resolved to the same serial port")
        self.observations.setdefault("ports", {}).update(resolved)
        return resolved

    def _attached_serials(self) -> set[str]:
        return {
            port.serial_number
            for port in self._flipper_ports()
            if port.serial_number is not None
        }

    def wait_for_disconnect(self, device: Device) -> float:
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            if device.serial not in self._attached_serials():
                return time.monotonic()
            time.sleep(0.25)
        raise HilError(f"timed out waiting for {device.role} USB disconnect")

    def wait_for_port(self, device: Device) -> str:
        deadline = time.monotonic() + self.timeout
        last_error = ""
        while time.monotonic() < deadline:
            try:
                return self.discover_ports({device.role: device})[device.role]
            except HilError as error:
                last_error = str(error)
                time.sleep(1)
        raise HilError(f"timed out waiting for {device.role}: {last_error}")

    def wait_for_dfu(self, device: Device) -> None:
        if not device.recovery:
            raise HilError(f"role {device.role} has no recovery configuration")
        self.require_program("dfu-util")
        deadline = time.monotonic() + self.timeout
        last_output = ""
        while time.monotonic() < deadline:
            result = self.run(
                f"detect {device.role} DFU",
                ["dfu-util", "--list"],
                timeout=20,
                check=False,
            )
            last_output = result.stdout
            if (
                "[0483:df11]" in last_output.lower()
                and f'serial="{device.recovery.dfu_serial}"' in last_output
            ):
                self.observations.setdefault("dfuSerials", {})[
                    device.role
                ] = device.recovery.dfu_serial
                return
            time.sleep(1)
        raise HilError(
            f"timed out waiting for DFU serial {device.recovery.dfu_serial!r}"
        )

    def power(self, device: Device, state: str) -> None:
        if state not in {"on", "off", "cycle"}:
            raise HilError(f"invalid USB power state: {state}")
        self.require_program("uhubctl")
        self.run(
            f"{device.role} USB power {state}",
            [
                "uhubctl",
                "-l",
                device.usb_power.location,
                "-p",
                str(device.usb_power.port),
                "-a",
                state,
            ],
            timeout=30,
        )
        time.sleep(device.usb_power.settle_seconds)

    def write_evidence(self, suite: str, status: str, error: str | None) -> Path:
        self.results_dir.mkdir(parents=True, exist_ok=True)
        timestamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime(self.started_at))
        path = self.results_dir / f"{timestamp}-{suite}.json"
        payload = {
            "schema": "poison.hil.result/v1",
            "suite": suite,
            "status": status,
            "startedAt": time.strftime(
                "%Y-%m-%dT%H:%M:%SZ", time.gmtime(self.started_at)
            ),
            "finishedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "error": error,
            "commands": self.commands,
            "observations": self.observations,
        }
        path.write_text(
            json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        return path


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", type=Path, default=DEFAULT_INVENTORY)
    parser.add_argument("--suite", choices=("baseline", "firmware-units", "javascript-limits", "javascript-workflow", "device-recovery", "rust-wasm", "rust-native", "v1-endurance", "v1-fault-matrix", "v1-recovery", "v1-performance", "local-only", "pair-control"))
    parser.add_argument("--release-duration", action="store_true", help="run the locked release-duration endurance profile")
    parser.add_argument("--performance-evidence", type=Path, default=Path("artifacts/release-evidence/performance.json"))
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--results-dir", type=Path, default=DEFAULT_RESULTS)
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args(argv)
    if not args.validate_only and not args.suite:
        parser.error("--suite is required unless --validate-only is used")
    if args.timeout < 10:
        parser.error("--timeout must be at least 10 seconds")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        devices = load_inventory(args.inventory)
    except InventoryError as error:
        print(f"HIL inventory error: {error}", file=sys.stderr)
        return 2
    print("HIL inventory valid: test and recovery roles configured")
    if args.validate_only:
        return 0
    if argv is None:
        try:
            _reexec_with_pinned_fbt_python(sys.argv[1:])
        except (HilError, OSError) as error:
            print(f"HIL runtime error: {error}", file=sys.stderr)
            return 2

    context = ExecutionContext(args.results_dir, args.timeout)
    context.performance_evidence = args.performance_evidence
    error_text = None
    status = "FAIL"
    try:
        if args.suite == "baseline":
            from suites import baseline as suite_module
        elif args.suite == "firmware-units":
            from suites import firmware_units as suite_module
        elif args.suite == "javascript-limits":
            from suites import javascript_limits as suite_module
        elif args.suite == "javascript-workflow":
            from suites import javascript_workflow as suite_module
        elif args.suite == "device-recovery":
            from suites import device_recovery as suite_module
        elif args.suite == "rust-wasm":
            from suites import rust_wasm as suite_module
        elif args.suite == "v1-endurance":
            from suites import v1_endurance as suite_module
        elif args.suite == "v1-fault-matrix":
            from suites import v1_fault_matrix as suite_module
        elif args.suite == "v1-recovery":
            from suites import v1_recovery as suite_module
        elif args.suite == "v1-performance":
            from suites import v1_performance as suite_module
        elif args.suite == "local-only":
            from suites import local_only as suite_module
        elif args.suite == "pair-control":
            from suites import pair_control as suite_module
        else:
            from suites import rust_native as suite_module
        suite_module.run(context, devices)
        status = "PASS"
        return_code = 0
    except (HilError, OSError) as error:
        error_text = str(error)
        print(f"HIL failure: {error}", file=sys.stderr)
        return_code = 1
    evidence = context.write_evidence(args.suite, status, error_text)
    print(f"HIL evidence: {evidence}")
    return return_code


if __name__ == "__main__":
    raise SystemExit(main())
