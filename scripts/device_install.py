#!/usr/bin/env python3

from __future__ import annotations

import argparse
import contextlib
import glob
import hashlib
import logging
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request


SCRIPTS_ROOT = Path(__file__).resolve().parent
if str(SCRIPTS_ROOT) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_ROOT))

from flipper.rpc import RPC_TIMEOUT, FlipperRpc, RpcError


FLIPPER_DFU_VID_PID = "0483:df11"
FLIPPER_RUNTIME_VID = 0x0483
FLIPPER_RUNTIME_PID = 0x5740
QFLIPPER_VERSION = "1.3.3"
QFLIPPER_MACOS_URL = (
    "https://update.flipperzero.one/builds/qFlipper/1.3.3/qFlipper-1.3.3.dmg"
)
QFLIPPER_MACOS_SHA256 = (
    "893dd16e79ccfeb26f4adc1e36a03390a51e8baf51303df824b9fac90d46d434"
)
POST_INSTALL_TIMEOUT = 600.0
POST_INSTALL_RETRY_DELAY = 1.0
POST_INSTALL_STATUS_INTERVAL = 15.0
RUNTIME_RECOVERY_TIMEOUT = 120.0
MANUAL_DFU_TIMEOUT = 180.0
EXPECTED_DEVICE_INFO = {
    "hardware_model": "Flipper Zero",
    "hardware_target": "7",
    "firmware_origin_fork": "PoisonedOS",
}
DEFAULT_MARAUDER_FAP = (
    SCRIPTS_ROOT.parent / "build/f7-firmware-D/.extapps/poison_esp_flasher.fap"
)
DEFAULT_MARAUDER_TARGET = "flipper-zero-wifi-dev-board"
UPDATE_ROOT = "/ext/update/poison-lkg"
UPDATE_RESOURCE_MILESTONES = {
    "manifest": "/ext/Manifest",
    "marauder-s2": (
        "/ext/apps_data/esp_flasher/assets/marauder/s2/"
        "esp32_marauder.flipper.bin"
    ),
    "marauder-s3": (
        "/ext/apps_data/esp_flasher/assets/marauder/s3/"
        "esp32_marauder.multiboardS3.bin"
    ),
    "marauder-wroom": (
        "/ext/apps_data/esp_flasher/assets/marauder/wroom/"
        "esp32_marauder.dev_board_pro.bin"
    ),
}


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(command, check=False, **kwargs)


def parse_flipper_dfu_serials(output: str) -> list[str]:
    pattern = re.compile(
        rf'Found DFU: \[{re.escape(FLIPPER_DFU_VID_PID)}\].*?serial="([^"]+)"'
    )
    return sorted(set(pattern.findall(output)))


def detect_flipper_dfu() -> str | None:
    dfu_util = shutil.which("dfu-util")
    if not dfu_util:
        return None
    result = run(
        [dfu_util, "--list"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    serials = parse_flipper_dfu_serials(result.stdout)
    if len(serials) > 1:
        raise RuntimeError(
            "Multiple Flipper DFU devices are connected; disconnect all but the target"
        )
    return serials[0] if serials else None


def detect_flipper_runtime(port: str = "auto") -> tuple[str, str] | None:
    from serial.tools import list_ports

    matches = []
    for candidate in list_ports.comports():
        if (
            candidate.vid == FLIPPER_RUNTIME_VID
            and candidate.pid == FLIPPER_RUNTIME_PID
            and candidate.serial_number
            and candidate.serial_number.startswith("flip_")
            and (port == "auto" or candidate.device == port)
        ):
            matches.append((candidate.device, candidate.serial_number))

    if len(matches) > 1:
        raise RuntimeError(
            "Multiple Flipper runtime devices are connected; use an explicit port"
        )
    return matches[0] if matches else None


def find_qflipper_cli() -> Path | None:
    if configured := os.environ.get("QFLIPPER_CLI"):
        candidate = Path(configured).expanduser()
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate

    if discovered := shutil.which("qFlipper-cli"):
        return Path(discovered)

    candidates = [
        Path("/Applications/qFlipper.app/Contents/MacOS/qFlipper-cli"),
        Path.home() / "Applications/qFlipper.app/Contents/MacOS/qFlipper-cli",
    ]
    candidates.extend(
        Path(path)
        for path in glob.glob(
            "/Volumes/qFlipper*/qFlipper.app/Contents/MacOS/qFlipper-cli"
        )
    )
    return next(
        (path for path in candidates if path.is_file() and os.access(path, os.X_OK)),
        None,
    )


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download_verified(url: str, destination: Path, expected_sha256: str) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and sha256_file(destination) == expected_sha256:
        return
    partial = destination.with_suffix(destination.suffix + ".part")
    with urllib.request.urlopen(url) as response, partial.open("wb") as output:
        shutil.copyfileobj(response, output)
    actual_sha256 = sha256_file(partial)
    if actual_sha256 != expected_sha256:
        partial.unlink(missing_ok=True)
        raise RuntimeError(
            f"qFlipper checksum mismatch: expected {expected_sha256}, got {actual_sha256}"
        )
    partial.replace(destination)


@contextlib.contextmanager
def qflipper_cli():
    if installed := find_qflipper_cli():
        yield installed
        return

    if sys.platform != "darwin":
        raise RuntimeError(
            "qFlipper-cli was not found; install qFlipper or set QFLIPPER_CLI"
        )

    cache_root = Path(
        os.environ.get(
            "POISON_TOOL_CACHE", Path(tempfile.gettempdir()) / "poisoned-os-tools"
        )
    )
    image = cache_root / f"qFlipper-{QFLIPPER_VERSION}.dmg"
    download_verified(QFLIPPER_MACOS_URL, image, QFLIPPER_MACOS_SHA256)
    attach = run(
        ["hdiutil", "attach", "-nobrowse", "-readonly", str(image)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if attach.returncode != 0:
        raise RuntimeError(f"Failed to mount qFlipper: {attach.stdout.strip()}")
    mount_points = [
        Path(line.split("\t")[-1])
        for line in attach.stdout.splitlines()
        if "\t/Volumes/" in line
    ]
    if not mount_points:
        raise RuntimeError("Mounted qFlipper image did not report a volume path")
    mount_point = mount_points[-1]
    cli = mount_point / "qFlipper.app/Contents/MacOS/qFlipper-cli"
    try:
        if not cli.is_file():
            raise RuntimeError(f"qFlipper CLI is missing from {mount_point}")
        yield cli
    finally:
        run(
            ["hdiutil", "detach", str(mount_point)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )


def run_qflipper_repair(expected_serial: str) -> int:
    current_serial = detect_flipper_dfu()
    if current_serial != expected_serial:
        raise RuntimeError("The selected Flipper DFU device changed before repair")
    with qflipper_cli() as cli:
        return run([str(cli), "-d", "1", "-c", "release"]).returncode


def wait_for_flipper_runtime(
    expected_serial: str | None, timeout: float = RUNTIME_RECOVERY_TIMEOUT
) -> str | None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        runtime = detect_flipper_runtime("auto")
        if runtime and (expected_serial is None or runtime[1] == expected_serial):
            return runtime[0]
        time.sleep(POST_INSTALL_RETRY_DELAY)
    return None


def wait_for_flipper_dfu(timeout: float = MANUAL_DFU_TIMEOUT) -> str | None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if serial := detect_flipper_dfu():
            return serial
        time.sleep(POST_INSTALL_RETRY_DELAY)
    return None


def wait_for_recovery_device(
    expected_runtime_serial: str | None,
    timeout: float = MANUAL_DFU_TIMEOUT,
) -> tuple[str, str] | None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if dfu_serial := detect_flipper_dfu():
            return "dfu", dfu_serial
        runtime = detect_flipper_runtime("auto")
        if runtime and (
            expected_runtime_serial is None or runtime[1] == expected_runtime_serial
        ):
            return "runtime", runtime[0]
        time.sleep(POST_INSTALL_RETRY_DELAY)
    return None


def run_qflipper_runtime_repair(expected_port: str, expected_serial: str) -> str | None:
    current_runtime = detect_flipper_runtime(expected_port)
    if current_runtime != (expected_port, expected_serial):
        raise RuntimeError("The selected Flipper runtime device changed before repair")
    with qflipper_cli() as cli:
        result = run([str(cli), "-d", "1", "-c", "release"]).returncode
    if result != 0:
        return None
    return wait_for_flipper_runtime(expected_serial)


def _require_flipper_identity(device_info: dict[str, str]) -> None:
    expected = {
        "hardware_model": "Flipper Zero",
        "hardware_target": "7",
    }
    mismatches = [
        f"{key}={device_info.get(key)!r} (expected {value!r})"
        for key, value in expected.items()
        if device_info.get(key) != value
    ]
    if mismatches:
        raise RpcError(", ".join(mismatches))


def run_rpc_update(port: str, manifest: Path, passthrough: list[str]) -> int:
    if passthrough:
        print(
            "The protobuf installer does not accept selfupdate CLI arguments: "
            + " ".join(passthrough),
            file=sys.stderr,
        )
        return 2
    runtime = detect_flipper_runtime(port)
    if not runtime:
        print(f"No exact Flipper runtime descriptor found for {port}", file=sys.stderr)
        return 5
    runtime_port, runtime_serial = runtime
    # The first PoisonedOS boot adopts this exact, complete update bundle as
    # its rollback artifact. Keep the manifest beside its DFU/resources;
    # update.fuf contains relative references to those files.
    remote_root = "/ext/update/poison-lkg"
    remote_manifest = f"{remote_root}/{manifest.name}"
    try:
        with FlipperRpc(runtime_port) as rpc:
            _require_flipper_identity(rpc.device_info())
            rpc.upload_tree(manifest.parent, remote_root)
            rpc.start_update(remote_manifest)
        print(f"PoisonedOS update sent to {runtime_serial} over protobuf RPC")
        return 0
    except (OSError, RpcError) as error:
        print(f"PoisonedOS RPC update failed: {error}", file=sys.stderr)
        return 5


def parse_device_info(output: bytes | str) -> dict[str, str]:
    if isinstance(output, bytes):
        output = output.decode("utf-8", errors="replace")
    properties: dict[str, str] = {}
    for raw_line in output.splitlines():
        if ":" not in raw_line:
            continue
        key, value = raw_line.split(":", 1)
        if key := key.strip():
            properties[key] = value.strip()
    return properties


def verify_poisoned_os(port: str, timeout: float = POST_INSTALL_TIMEOUT) -> bool:
    deadline = time.monotonic() + timeout
    last_error = "Flipper CDC did not appear"
    next_status = 0.0

    while time.monotonic() < deadline:
        runtime_present = False
        try:
            runtime = detect_flipper_runtime(port)
            if not runtime:
                raise RuntimeError("Flipper CDC did not appear")
            runtime_present = True
            resolved_port, _runtime_serial = runtime
            remaining = deadline - time.monotonic()
            if remaining <= 0.0:
                break
            with FlipperRpc(resolved_port, timeout=min(RPC_TIMEOUT, remaining)) as rpc:
                device_info = rpc.device_info()
            mismatches = [
                f"{key}={device_info.get(key)!r} (expected {value!r})"
                for key, value in EXPECTED_DEVICE_INFO.items()
                if device_info.get(key) != value
            ]
            if mismatches:
                raise RuntimeError(", ".join(mismatches))
            if not device_info.get("firmware_version"):
                raise RuntimeError("device_info did not report firmware_version")
            print(
                "Verified PoisonedOS on "
                f"{resolved_port}: {device_info['firmware_version']}"
            )
            return True
        except Exception as error:
            last_error = str(error)
            now = time.monotonic()
            if (
                runtime_present
                and "Flipper CLI marker" in last_error
                and now >= next_status
            ):
                print(
                    "Flipper runtime is present but its CLI/RPC endpoint is unavailable; "
                    "if a PIN lock is shown, unlock the device to complete installed-firmware "
                    "verification.",
                    file=sys.stderr,
                )
                next_status = now + POST_INSTALL_STATUS_INTERVAL
        remaining = deadline - time.monotonic()
        if remaining <= 0.0:
            break
        time.sleep(min(POST_INSTALL_RETRY_DELAY, remaining))

    print(
        f"PoisonedOS did not return over CDC within {timeout:.0f}s: {last_error}",
        file=sys.stderr,
    )
    return False


def run_install_attempt(port: str, manifest: Path, passthrough: list[str]) -> int:
    result = run_rpc_update(port, manifest, passthrough)
    if result != 0:
        return result
    return 0 if verify_poisoned_os(port) else 6


def collect_runtime_diagnostics(port: str) -> bool:
    with FlipperRpc(port) as rpc:
        version = rpc.protobuf_version()
        device_info = rpc.device_info()
        _require_flipper_identity(device_info)
        storage_info = rpc.storage_info("/ext")
        update_entries = rpc.list_dir(UPDATE_ROOT)
        milestones = {
            name: rpc.stat(path) for name, path in UPDATE_RESOURCE_MILESTONES.items()
        }

    print(
        "Doctor runtime: "
        f"port={port} protobuf={version[0]}.{version[1]} "
        f"firmware_version={device_info.get('firmware_version', 'unknown')} "
        f"firmware_origin_fork={device_info.get('firmware_origin_fork', 'unknown')}"
    )
    print(
        "Doctor storage: "
        f"total={storage_info['total_space']} free={storage_info['free_space']}"
    )
    if update_entries:
        for entry in sorted(update_entries, key=lambda item: str(item["name"])):
            print(
                "Doctor update artifact: "
                f"{entry['name']} type={entry['type']} size={entry['size']}"
            )
    else:
        print(f"Doctor update artifacts: {UPDATE_ROOT} is absent or empty")
    for name, file_info in milestones.items():
        state = "present" if file_info else "absent"
        details = f" size={file_info['size']}" if file_info else ""
        print(f"Doctor resource milestone: {name}={state}{details}")
    return True


def _print_manual_recovery_sequence() -> None:
    print(
        "Exact Flipper is absent. Unplug Flipper USB, hold BACK for 30 seconds, "
        "then hold BACK + OK for 30 seconds, release both, and reconnect USB. "
        f"Waiting up to {MANUAL_DFU_TIMEOUT:.0f}s for Flipper runtime or DFU "
        f"{FLIPPER_DFU_VID_PID}.",
        file=sys.stderr,
    )


def recover_after_failed_update(expected_runtime_serial: str | None) -> str | None:
    print("Post-install verification failed; starting exact-device recovery", file=sys.stderr)
    recovery_device = None
    if dfu_serial := detect_flipper_dfu():
        recovery_device = ("dfu", dfu_serial)
    else:
        runtime = detect_flipper_runtime("auto")
        if runtime and (
            expected_runtime_serial is None or runtime[1] == expected_runtime_serial
        ):
            recovery_device = ("runtime", runtime[0])

    if recovery_device is None:
        _print_manual_recovery_sequence()
        recovery_device = wait_for_recovery_device(expected_runtime_serial)
    if recovery_device is None:
        print("Doctor did not observe Flipper runtime or DFU", file=sys.stderr)
        return None

    state, identifier = recovery_device
    if state == "dfu":
        return recover_flipper_dfu(
            identifier, expected_runtime_serial=expected_runtime_serial
        )

    try:
        collect_runtime_diagnostics(identifier)
        return identifier
    except (OSError, RuntimeError, RpcError) as error:
        print(
            f"Recovered runtime is not diagnosable over RPC: {error}; "
            "restoring official firmware with qFlipper",
            file=sys.stderr,
        )
    runtime = detect_flipper_runtime(identifier)
    if not runtime:
        return None
    return run_qflipper_runtime_repair(runtime[0], runtime[1])


def doctor(port: str, recover: bool = True) -> int:
    try:
        if dfu_serial := detect_flipper_dfu():
            print(f"Doctor state: Flipper DFU {dfu_serial}")
            if not recover:
                return 2
            runtime_port = recover_flipper_dfu(dfu_serial)
            collect_runtime_diagnostics(runtime_port)
            return 0

        if runtime := detect_flipper_runtime(port):
            print(f"Doctor state: Flipper runtime {runtime[1]} on {runtime[0]}")
            collect_runtime_diagnostics(runtime[0])
            return 0

        print("Doctor state: exact Flipper is absent", file=sys.stderr)
        if not recover:
            return 3
        _print_manual_recovery_sequence()
        recovery_device = wait_for_recovery_device(None)
        if recovery_device is None:
            print("Doctor did not observe Flipper runtime or DFU", file=sys.stderr)
            return 3
        state, identifier = recovery_device
        runtime_port = (
            recover_flipper_dfu(identifier) if state == "dfu" else identifier
        )
        collect_runtime_diagnostics(runtime_port)
        return 0
    except (OSError, RuntimeError, RpcError) as error:
        print(f"Doctor failed: {error}", file=sys.stderr)
        return 4


def install(port: str, manifest: Path, passthrough: list[str]) -> int:
    if not manifest.is_file():
        print(f"Update manifest not found: {manifest}", file=sys.stderr)
        return 2

    try:
        dfu_serial = detect_flipper_dfu()
        if dfu_serial:
            print(f"Recovering Flipper DFU device {dfu_serial} with qFlipper")
            active_port = recover_flipper_dfu(dfu_serial)
        else:
            active_port = port
        selected_runtime = detect_flipper_runtime(active_port)
        expected_runtime_serial = selected_runtime[1] if selected_runtime else None
        result = run_install_attempt(active_port, manifest, passthrough)
        if result == 0:
            return 0
        if result == 6:
            recover_after_failed_update(expected_runtime_serial)
            return result
        dfu_serial = detect_flipper_dfu()
        if dfu_serial:
            print(f"Install entered DFU; recovering {dfu_serial} with qFlipper")
            active_port = recover_flipper_dfu(dfu_serial)
        else:
            runtime = detect_flipper_runtime(active_port)
            if not runtime:
                return result
            runtime_port, runtime_serial = runtime
            print(
                "Flipper USB receive path is unresponsive. Unplug USB, hold "
                "OK + BACK for 30 seconds, then reconnect USB. Waiting up to "
                f"{MANUAL_DFU_TIMEOUT:.0f}s for exact Flipper DFU 0483:df11.",
                file=sys.stderr,
            )
            recovery_dfu_serial = wait_for_flipper_dfu()
            if not recovery_dfu_serial:
                print("Flipper DFU device did not appear", file=sys.stderr)
                return 3
            print(
                f"Recovering verified Flipper DFU {recovery_dfu_serial} with qFlipper"
            )
            active_port = recover_flipper_dfu(
                recovery_dfu_serial, expected_runtime_serial=runtime_serial
            )

        result = run_install_attempt(active_port, manifest, passthrough)
        if result == 0:
            return 0
        if result == 6:
            recover_after_failed_update(expected_runtime_serial)
            return result

        # A second failed custom install is not retried. If it returned to DFU,
        # restore official firmware once more so failure leaves a bootable device.
        dfu_serial = detect_flipper_dfu()
        if dfu_serial:
            print(
                f"Retry entered DFU; restoring {dfu_serial} with qFlipper",
                file=sys.stderr,
            )
            recover_flipper_dfu(dfu_serial)
        return result
    except (OSError, RuntimeError) as error:
        print(f"Device install failed: {error}", file=sys.stderr)
        return 4


def _probe_rpc(port: str) -> tuple[tuple[int, int], dict[str, str]]:
    with FlipperRpc(port) as rpc:
        version = rpc.protobuf_version()
        device_info = rpc.device_info()
    _require_flipper_identity(device_info)
    return version, device_info


def recover_flipper_dfu(
    expected_dfu_serial: str, expected_runtime_serial: str | None = None
) -> str:
    qflipper_status = run_qflipper_repair(expected_dfu_serial)
    recovered_port = wait_for_flipper_runtime(expected_runtime_serial)
    if not recovered_port:
        if qflipper_status:
            failure = f"qFlipper exited with status {qflipper_status}; "
        else:
            failure = ""
        raise RuntimeError(f"{failure}Flipper runtime did not return after recovery")

    try:
        _probe_rpc(recovered_port)
    except (OSError, RuntimeError, RpcError) as error:
        raise RuntimeError(
            "Flipper runtime returned after recovery but failed RPC identity: "
            f"{error}"
        ) from error

    if qflipper_status:
        print(
            "qFlipper reported status "
            f"{qflipper_status}, but the exact Flipper runtime returned and "
            "passed live protobuf identity verification; continuing",
            file=sys.stderr,
        )
    return recovered_port


def preflight(port: str) -> int:
    try:
        dfu_serial = detect_flipper_dfu()
        if dfu_serial:
            print(
                f"Preflight found Flipper DFU {dfu_serial}; recovering it before build"
            )
            port = recover_flipper_dfu(dfu_serial)

        runtime = detect_flipper_runtime(port)
        if not runtime:
            raise RuntimeError(f"No exact Flipper runtime descriptor found for {port}")
        runtime_port, runtime_serial = runtime
        version, device_info = _probe_rpc(runtime_port)
        firmware = device_info.get("firmware_version", "unknown")
        print(
            "Live Flipper preflight passed: "
            f"{runtime_serial} on {runtime_port}, protobuf {version[0]}.{version[1]}, "
            f"firmware {firmware}"
        )
        return 0
    except (OSError, RuntimeError, RpcError) as error:
        print(f"Live Flipper preflight failed before build: {error}", file=sys.stderr)
        return 4


def provision_marauder(port: str, fap: Path, target: str) -> int:
    import marauder

    arguments = ["flash", "--target", target, "--fap", str(fap)]
    if port != "auto":
        arguments.extend(("--port", port))
    return marauder.main(arguments)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Install a Flipper update with automatic DFU recovery"
    )
    parser.add_argument("-p", "--port", default="auto")
    parser.add_argument("--preflight-only", action="store_true")
    parser.add_argument("--doctor", action="store_true")
    parser.add_argument("--diagnose-only", action="store_true")
    parser.add_argument("--skip-marauder", action="store_true")
    parser.add_argument("--marauder-fap", type=Path, default=DEFAULT_MARAUDER_FAP)
    parser.add_argument("--marauder-target", default=DEFAULT_MARAUDER_TARGET)
    parser.add_argument("manifest_path", type=Path, nargs="?")
    args, passthrough = parser.parse_known_args(argv)
    if args.preflight_only:
        if args.manifest_path is not None or passthrough:
            parser.error("--preflight-only does not accept an update manifest")
        return preflight(args.port)
    if args.doctor:
        if args.manifest_path is not None or passthrough:
            parser.error("--doctor does not accept an update manifest")
        return doctor(args.port, recover=not args.diagnose_only)
    if args.diagnose_only:
        parser.error("--diagnose-only requires --doctor")
    if args.manifest_path is None:
        parser.error("an update manifest is required")
    result = install(args.port, args.manifest_path.resolve(), passthrough)
    if result or args.skip_marauder:
        return result
    marauder_result = provision_marauder(
        args.port, args.marauder_fap, args.marauder_target
    )
    return 0 if marauder_result == 0 else 7


if __name__ == "__main__":
    raise SystemExit(main())
