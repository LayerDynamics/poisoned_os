#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
import urllib.request
import zipfile
from pathlib import Path
from typing import NamedTuple

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))


REPOSITORY_ROOT = SCRIPT_DIR.parent
from flipper.rpc import FlipperRpc, RpcError


DEFAULT_LOCK_PATH = REPOSITORY_ROOT / "provenance" / "marauder.lock.json"
DEFAULT_CACHE_ROOT = REPOSITORY_ROOT / "dist" / "marauder"
EXPECTED_REPOSITORY = "justcallmekoko/ESP32Marauder"
EXPECTED_MANIFEST_KIND = "esp32-marauder-installer-release"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
CHIP_RE = re.compile(r"Chip is (ESP32(?:-[A-Z0-9]+)?)", re.IGNORECASE)
FLASH_SIZE_RE = re.compile(r"Detected flash size:\s*(\d+)(KB|MB)", re.IGNORECASE)
VERSION_RE = re.compile(r"^Version:\s*(v[^\s]+)\s*$", re.MULTILINE)
HARDWARE_RE = re.compile(r"^Hardware:\s*(.+?)\s*$", re.MULTILINE)
BRIDGE_EXIT_BAUD = 1200
ON_DEVICE_FAP = REPOSITORY_ROOT / "build/f7-firmware-D/.extapps/poison_esp_flasher.fap"
ON_DEVICE_FAP_PATH = "/ext/apps/GPIO/poison_esp_flasher.fap"
ON_DEVICE_ASSET_ROOT = "/ext/apps_data/esp_flasher/assets/marauder"
ON_DEVICE_STATUS_PATH = "/ext/apps_data/esp_flasher/marauder-status.txt"
ON_DEVICE_FLASH_TIMEOUT = 600.0
FLIPPER_TARGET_ID = "flipper-zero-wifi-dev-board"
ON_DEVICE_SEGMENT_PATHS = {
    0x1000: f"{ON_DEVICE_ASSET_ROOT}/s2/esp32_marauder.ino.bootloader.bin",
    0x8000: f"{ON_DEVICE_ASSET_ROOT}/esp32_marauder.ino.partitions.bin",
    0xE000: f"{ON_DEVICE_ASSET_ROOT}/boot_app0.bin",
    0x10000: f"{ON_DEVICE_ASSET_ROOT}/s2/esp32_marauder.flipper.bin",
}


class MarauderError(RuntimeError):
    pass


class ManifestError(MarauderError):
    pass


class AssetVerificationError(MarauderError):
    pass


class AmbiguousTargetError(MarauderError):
    pass


class TargetMismatchError(MarauderError):
    pass


class TransportError(MarauderError):
    pass


class MarauderInfo(NamedTuple):
    version: str
    hardware_name: str


class VerifiedSegment(NamedTuple):
    role: str
    offset: int
    path: Path
    size: int
    sha256: str


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_stream(source):
    digest = hashlib.sha256()
    for block in iter(lambda: source.read(1024 * 1024), b""):
        digest.update(block)
    return digest.hexdigest()


def load_release_lock(path=DEFAULT_LOCK_PATH):
    path = Path(path)
    try:
        lock = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ManifestError(f"cannot read release lock {path}: {error}") from error

    required = (
        "schemaVersion",
        "sourceRepository",
        "sourceCommit",
        "version",
        "channel",
        "installerBundleUrl",
        "installerBundleSha256",
        "manifestPath",
    )
    missing = [field for field in required if field not in lock]
    if missing:
        raise ManifestError(f"release lock missing fields: {', '.join(missing)}")
    if lock["schemaVersion"] != 1:
        raise ManifestError("unsupported release lock schemaVersion")
    if lock["sourceRepository"] != EXPECTED_REPOSITORY:
        raise ManifestError("unexpected release lock sourceRepository")
    if not SHA256_RE.fullmatch(lock["installerBundleSha256"]):
        raise ManifestError("invalid installerBundleSha256")
    if Path(lock["manifestPath"]).name != lock["manifestPath"]:
        raise ManifestError("manifestPath must be a base name")
    return lock


def _require_equal(manifest, lock, field):
    if manifest.get(field) != lock.get(field):
        raise ManifestError(
            f"manifest {field} does not match pinned release: "
            f"{manifest.get(field)!r} != {lock.get(field)!r}"
        )


def _validate_segment(segment, target_id, mode):
    required = ("role", "offset", "size", "sha256", "fileName")
    missing = [field for field in required if field not in segment]
    if missing:
        raise ManifestError(
            f"target {target_id} {mode} segment missing fields: {', '.join(missing)}"
        )
    if not isinstance(segment["offset"], int) or segment["offset"] < 0:
        raise ManifestError(f"target {target_id} has invalid segment offset")
    if not isinstance(segment["size"], int) or segment["size"] <= 0:
        raise ManifestError(f"target {target_id} has invalid segment size")
    if not isinstance(segment["sha256"], str) or not SHA256_RE.fullmatch(
        segment["sha256"]
    ):
        raise ManifestError(f"target {target_id} has invalid segment sha256")
    file_name = segment["fileName"]
    if not isinstance(file_name, str) or Path(file_name).name != file_name:
        raise ManifestError(f"target {target_id} has unsafe segment fileName")


def load_manifest(path, lock):
    path = Path(path)
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ManifestError(f"cannot read Marauder manifest {path}: {error}") from error

    if manifest.get("schemaVersion") != 1:
        raise ManifestError("unsupported Marauder manifest schemaVersion")
    if manifest.get("kind") != EXPECTED_MANIFEST_KIND:
        raise ManifestError("unexpected Marauder manifest kind")
    if manifest.get("metadataStatus") != "authoritative":
        raise ManifestError("Marauder manifest metadata is not authoritative")
    for field in ("sourceRepository", "sourceCommit", "version", "channel"):
        _require_equal(manifest, lock, field)

    targets = manifest.get("targets")
    if not isinstance(targets, list) or not targets:
        raise ManifestError("Marauder manifest targets must be a non-empty list")
    target_ids = set()
    for target in targets:
        target_id = target.get("id")
        if not isinstance(target_id, str) or not target_id:
            raise ManifestError("Marauder target has invalid id")
        if target_id in target_ids:
            raise ManifestError(f"duplicate Marauder target id {target_id}")
        target_ids.add(target_id)
        if not isinstance(target.get("chipFamily"), str):
            raise ManifestError(f"target {target_id} has invalid chipFamily")
        if not isinstance(target.get("esptoolChip"), str):
            raise ManifestError(f"target {target_id} has invalid esptoolChip")
        flash = target.get("flash")
        if not isinstance(flash, dict):
            raise ManifestError(f"target {target_id} has no flash configuration")
        if not isinstance(flash.get("sizeBytes"), int) or flash["sizeBytes"] <= 0:
            raise ManifestError(f"target {target_id} has invalid flash size")
        for mode in ("update", "factory"):
            if mode not in flash:
                continue
            mode_config = flash[mode]
            segments = mode_config.get("segments")
            if not isinstance(segments, list) or not segments:
                raise ManifestError(f"target {target_id} {mode} has no segments")
            offsets = set()
            for segment in segments:
                _validate_segment(segment, target_id, mode)
                if segment["offset"] in offsets:
                    raise ManifestError(
                        f"target {target_id} {mode} has duplicate segment offset"
                    )
                offsets.add(segment["offset"])
    return manifest


def parse_marauder_info(output):
    version = VERSION_RE.search(output)
    hardware = HARDWARE_RE.search(output)
    if not version or not hardware:
        return None
    return MarauderInfo(version.group(1), hardware.group(1).strip())


def parse_esptool_chip(output):
    match = CHIP_RE.search(output)
    return match.group(1).upper() if match else None


def parse_flash_size(output):
    match = FLASH_SIZE_RE.search(output)
    if not match:
        return None
    value = int(match.group(1))
    multiplier = 1024 if match.group(2).upper() == "KB" else 1024 * 1024
    return value * multiplier


def _target_names(target):
    return [target["displayName"], *target.get("aliases", [])]


def _check_target_identity(target, chip_family=None, flash_size=None):
    if chip_family and target["chipFamily"].casefold() != chip_family.casefold():
        raise TargetMismatchError(
            f"detected {chip_family}, but target {target['id']} requires "
            f"{target['chipFamily']}"
        )
    expected_size = target["flash"]["sizeBytes"]
    if flash_size and expected_size != flash_size:
        raise TargetMismatchError(
            f"detected {flash_size} byte flash, but target {target['id']} requires "
            f"{expected_size} bytes"
        )
    return target


def select_target(
    manifest,
    hardware_name=None,
    chip_family=None,
    flash_size=None,
    requested=None,
):
    targets = manifest["targets"]
    if requested:
        matches = [target for target in targets if target["id"] == requested]
        if not matches:
            raise TargetMismatchError(f"unknown Marauder target {requested}")
        return _check_target_identity(matches[0], chip_family, flash_size)

    if hardware_name:
        folded = hardware_name.casefold()
        matches = [
            target
            for target in targets
            if any(name.casefold() == folded for name in _target_names(target))
        ]
        if len(matches) != 1:
            raise TargetMismatchError(
                f"Marauder hardware name {hardware_name!r} does not identify one target"
            )
        return _check_target_identity(matches[0], chip_family, flash_size)

    matches = targets
    if chip_family:
        matches = [
            target
            for target in matches
            if target["chipFamily"].casefold() == chip_family.casefold()
        ]
    if flash_size:
        matches = [
            target for target in matches if target["flash"]["sizeBytes"] == flash_size
        ]
    if len(matches) == 1:
        return matches[0]
    if not matches:
        raise TargetMismatchError("no Marauder target matches detected hardware")
    ids = ", ".join(sorted(target["id"] for target in matches))
    raise AmbiguousTargetError(
        f"detected chip does not uniquely identify the board; candidates: {ids}"
    )


def verify_target_assets(target, asset_root, mode):
    flash_mode = target["flash"].get(mode)
    if not flash_mode:
        raise AssetVerificationError(f"target {target['id']} has no {mode} image set")
    asset_root = Path(asset_root)
    verified = []
    for segment in flash_mode["segments"]:
        path = asset_root / segment["fileName"]
        try:
            actual_size = path.stat().st_size
        except OSError as error:
            raise AssetVerificationError(
                f"cannot stat segment {path}: {error}"
            ) from error
        if actual_size != segment["size"]:
            raise AssetVerificationError(
                f"segment {path.name} size mismatch: {actual_size} != {segment['size']}"
            )
        actual_hash = sha256_file(path)
        if actual_hash != segment["sha256"]:
            raise AssetVerificationError(
                f"segment {path.name} SHA-256 mismatch: {actual_hash} != "
                f"{segment['sha256']}"
            )
        verified.append(
            VerifiedSegment(
                segment["role"],
                segment["offset"],
                path,
                actual_size,
                actual_hash,
            )
        )
    return verified


def _esptool_prefix(python, port, target, baud):
    return [
        python,
        "-m",
        "esptool",
        "--chip",
        target["esptoolChip"],
        "--port",
        port,
        "--baud",
        str(baud),
    ]


def build_esptool_commands(python, port, target, mode, segments, baud=460800):
    mode_config = target["flash"][mode]
    prefix = _esptool_prefix(python, port, target, baud)
    commands = []
    if mode_config.get("erase"):
        commands.append([*prefix, "erase_flash"])

    flash_size_mb = target["flash"]["sizeBytes"] // (1024 * 1024)
    write = [
        *prefix,
        "--before",
        "default_reset",
        "--after",
        "hard_reset",
        "write_flash",
        "--flash_mode",
        target["flash"]["mode"],
        "--flash_freq",
        target["flash"]["frequency"],
        "--flash_size",
        f"{flash_size_mb}MB",
    ]
    verify = [*prefix, "verify_flash"]
    for segment in segments:
        offset = f"0x{segment.offset:x}"
        write.extend((offset, str(segment.path)))
        verify.extend((offset, str(segment.path)))
    commands.extend((write, verify))
    return commands


def _download_verified_bundle(lock, cache_root):
    release_dir = Path(cache_root) / lock["version"]
    release_dir.mkdir(parents=True, exist_ok=True)
    bundle_path = release_dir / "marauder-installer-assets.zip"
    if bundle_path.exists():
        actual_hash = sha256_file(bundle_path)
        if actual_hash != lock["installerBundleSha256"]:
            raise AssetVerificationError(
                f"cached installer bundle SHA-256 mismatch: {actual_hash}"
            )
        return bundle_path, release_dir

    partial_path = release_dir / "marauder-installer-assets.zip.partial"
    if partial_path.exists():
        raise AssetVerificationError(
            f"partial installer download already exists: {partial_path}"
        )
    request = urllib.request.Request(
        lock["installerBundleUrl"], headers={"User-Agent": "PoisonedOS-Marauder/1"}
    )
    try:
        with urllib.request.urlopen(request, timeout=60) as response, partial_path.open(
            "xb"
        ) as destination:
            while block := response.read(1024 * 1024):
                destination.write(block)
            destination.flush()
            os.fsync(destination.fileno())
    except (OSError, urllib.error.URLError) as error:
        raise AssetVerificationError(
            f"cannot download installer bundle: {error}"
        ) from error
    actual_hash = sha256_file(partial_path)
    if actual_hash != lock["installerBundleSha256"]:
        raise AssetVerificationError(
            f"downloaded installer bundle SHA-256 mismatch: {actual_hash}"
        )
    os.replace(partial_path, bundle_path)
    return bundle_path, release_dir


def _extract_bundle_without_overwrite(bundle_path, release_dir):
    asset_root = release_dir / "assets"
    asset_root.mkdir(exist_ok=True)
    with zipfile.ZipFile(bundle_path) as archive:
        for member in archive.infolist():
            member_path = Path(member.filename)
            if member.is_dir():
                continue
            if member_path.name != member.filename:
                raise AssetVerificationError(
                    f"installer bundle has unsafe member {member.filename!r}"
                )
            destination = asset_root / member.filename
            if destination.exists():
                try:
                    actual_size = destination.stat().st_size
                    actual_hash = sha256_file(destination)
                    with archive.open(member) as source:
                        expected_hash = sha256_stream(source)
                except OSError as error:
                    raise AssetVerificationError(
                        f"cannot verify extracted cache member {member.filename}: {error}"
                    ) from error
                if actual_size != member.file_size or actual_hash != expected_hash:
                    raise AssetVerificationError(
                        f"extracted cache member {member.filename} does not match "
                        "verified bundle"
                    )
                continue
            with archive.open(member) as source, destination.open("xb") as output:
                while block := source.read(1024 * 1024):
                    output.write(block)
                output.flush()
                os.fsync(output.fileno())
    return asset_root


def prepare_assets(lock_path=DEFAULT_LOCK_PATH, cache_root=DEFAULT_CACHE_ROOT):
    lock = load_release_lock(lock_path)
    bundle_path, release_dir = _download_verified_bundle(lock, cache_root)
    asset_root = _extract_bundle_without_overwrite(bundle_path, release_dir)
    manifest = load_manifest(asset_root / lock["manifestPath"], lock)
    return lock, manifest, asset_root


def _serial_module():
    try:
        import serial
        import serial.tools.list_ports
    except ImportError as error:
        raise TransportError("pyserial is required for the Flipper bridge") from error
    return serial


def find_flipper_port(requested=None):
    serial = _serial_module()
    ports = list(serial.tools.list_ports.comports())
    if requested:
        matching = [port for port in ports if port.device == requested]
        if matching and matching[0].vid == 0x0483 and matching[0].pid == 0x5740:
            return requested
        if matching:
            raise TransportError(
                f"requested serial port is not a Flipper descriptor: {requested}"
            )
        raise TransportError(f"requested serial port is not present: {requested}")
    matches = []
    for port in ports:
        description = " ".join(
            value
            for value in (port.description, port.manufacturer, port.product)
            if value
        ).casefold()
        if "flipper" in description or (port.vid == 0x0483 and port.pid == 0x5740):
            matches.append(port.device)
    if len(matches) != 1:
        raise TransportError(
            f"expected one connected Flipper serial port, found {len(matches)}: {matches}"
        )
    return matches[0]


def _read_serial(serial_port, duration):
    deadline = time.monotonic() + duration
    chunks = []
    while time.monotonic() < deadline:
        try:
            data = serial_port.read(4096)
        except OSError:
            break
        if data:
            chunks.append(data)
        else:
            time.sleep(0.05)
    return b"".join(chunks).decode("utf-8", errors="replace")


def start_marauder_bridge(port):
    serial = _serial_module()
    try:
        with serial.Serial(port, 230400, timeout=0.1, write_timeout=1) as connection:
            connection.reset_input_buffer()
            connection.write(b"loader info\r")
            connection.flush()
            info = _read_serial(connection, 0.8)
            if 'Application "' in info:
                raise TransportError(
                    "an application is already running; close it before provisioning"
                )
            connection.write(b'loader open "/ext/apps/GPIO/gpio.fap" marauder_bridge\r')
            connection.flush()
            _read_serial(connection, 0.5)
    except OSError as error:
        if not Path(port).exists():
            pass
        else:
            raise TransportError(f"cannot start Marauder bridge: {error}") from error

    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if Path(port).exists():
            time.sleep(0.3)
            return port
        time.sleep(0.1)
    raise TransportError("Flipper USB-UART bridge did not enumerate")


def query_marauder_info(port, timeout=3.0):
    serial = _serial_module()
    try:
        with serial.Serial(
            port,
            115200,
            timeout=0.1,
            write_timeout=1,
            dsrdtr=False,
            rtscts=False,
        ) as connection:
            connection.dtr = False
            connection.rts = False
            time.sleep(0.4)
            connection.reset_input_buffer()
            connection.write(b"\ninfo\n")
            connection.flush()
            output = _read_serial(connection, timeout)
    except OSError as error:
        raise TransportError(f"cannot query attached ESP UART: {error}") from error
    return parse_marauder_info(output), output


def run_checked(command):
    process = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    print(process.stdout, end="")
    if process.returncode:
        raise TransportError(
            f"command failed with exit {process.returncode}: {' '.join(command)}"
        )
    return process.stdout


def detect_esp(python, port):
    base = [python, "-m", "esptool", "--chip", "auto", "--port", port]
    chip_output = run_checked([*base, "chip_id"])
    chip = parse_esptool_chip(chip_output)
    if not chip:
        raise TransportError("esptool did not report an ESP chip family")
    flash_output = run_checked([*base, "flash_id"])
    flash_size = parse_flash_size(flash_output)
    if not flash_size:
        raise TransportError("esptool did not report flash size")
    return chip, flash_size


def exit_marauder_bridge(port):
    serial = _serial_module()
    try:
        with serial.Serial(port, BRIDGE_EXIT_BAUD, timeout=0.1):
            time.sleep(0.3)
    except OSError as error:
        raise TransportError(f"cannot signal bridge exit: {error}") from error


def bridge_provision(args):
    lock, manifest, asset_root = prepare_assets(args.lock, args.cache)
    port = find_flipper_port(args.port)
    bridge_port = start_marauder_bridge(port)
    already_installed = False
    try:
        current_info, raw_info = query_marauder_info(bridge_port)
        if raw_info.strip():
            print(raw_info, end="" if raw_info.endswith("\n") else "\n")

        if current_info and current_info.version == lock["version"]:
            target = select_target(
                manifest,
                hardware_name=current_info.hardware_name,
                requested=args.target,
            )
            print(
                f"Marauder {current_info.version} is already installed for "
                f"{target['id']}"
            )
            already_installed = True
        else:
            chip_family, flash_size = detect_esp(args.python, bridge_port)
            target = select_target(
                manifest,
                hardware_name=current_info.hardware_name if current_info else None,
                chip_family=chip_family,
                flash_size=flash_size,
                requested=args.target,
            )
            mode = "update" if current_info else "factory"
            segments = verify_target_assets(target, asset_root, mode)
            print(
                f"Provisioning {lock['version']} to {target['displayName']} "
                f"({chip_family}, {flash_size} bytes) using {mode} layout"
            )
            for command in build_esptool_commands(
                args.python,
                bridge_port,
                target,
                mode,
                segments,
                args.baud,
            ):
                run_checked(command)
    finally:
        exit_marauder_bridge(bridge_port)

    if not already_installed:
        print("Marauder flash and readback verification completed")
    return 0


def _on_device_segments(manifest, asset_root, requested_target):
    if requested_target and requested_target != FLIPPER_TARGET_ID:
        raise TargetMismatchError(
            "automatic on-device flashing currently requires the explicit "
            f"{FLIPPER_TARGET_ID} target; omit --target to use the board menu"
        )
    target = select_target(manifest, requested=requested_target or FLIPPER_TARGET_ID)
    segments = verify_target_assets(target, asset_root, "factory")
    offsets = {segment.offset for segment in segments}
    expected = set(ON_DEVICE_SEGMENT_PATHS)
    if offsets != expected:
        raise AssetVerificationError(
            "Flipper WiFi Dev Board factory layout does not match the on-device "
            f"flasher: {sorted(offsets)} != {sorted(expected)}"
        )
    return segments


def _wait_for_on_device_flash(port, timeout=ON_DEVICE_FLASH_TIMEOUT):
    deadline = time.monotonic() + timeout
    last_error = "status marker has not appeared"
    while time.monotonic() < deadline:
        try:
            with FlipperRpc(
                port, timeout=min(10.0, deadline - time.monotonic())
            ) as rpc:
                status = rpc.read_file(ON_DEVICE_STATUS_PATH)
            if status == b"ok\n":
                return
            if status and status.startswith(b"error:"):
                raise TransportError(
                    "on-device Marauder flash failed: "
                    + status.decode("ascii", errors="replace").strip()
                )
            if status not in (None, b"running\n"):
                raise TransportError(
                    "on-device Marauder flash returned an invalid status marker: "
                    + repr(status)
                )
        except (OSError, RpcError) as error:
            last_error = str(error)
        time.sleep(1.0)
    raise TransportError(
        f"on-device Marauder flash did not complete within {timeout:.0f}s: {last_error}"
    )


def provision(args):
    _, manifest, asset_root = prepare_assets(args.lock, args.cache)
    fap_path = Path(args.fap)
    if not fap_path.is_file():
        raise TransportError(
            f"on-device flasher is not built: {fap_path}; run ./fbt fap_poison_esp_flasher"
        )

    segments = _on_device_segments(manifest, asset_root, args.target)
    port = find_flipper_port(args.port)
    try:
        with FlipperRpc(port) as rpc:
            rpc.upload_file(fap_path, ON_DEVICE_FAP_PATH)
            for segment in segments:
                rpc.upload_file(segment.path, ON_DEVICE_SEGMENT_PATHS[segment.offset])

            launch_arg = (
                "marauder_flipper"
                if args.target == FLIPPER_TARGET_ID
                else "select_board"
            )
            if args.target == FLIPPER_TARGET_ID:
                rpc.delete_tree(ON_DEVICE_STATUS_PATH)
            rpc.start_app(ON_DEVICE_FAP_PATH, launch_arg)
    except (OSError, RpcError) as error:
        raise TransportError(
            f"cannot stage or launch on-device flasher: {error}"
        ) from error

    if args.target == FLIPPER_TARGET_ID:
        _wait_for_on_device_flash(port)
        print("Verified on-device Marauder flash for Flipper WiFi Dev Board")
    else:
        print("Opened on-device ESP Flasher board-selection menu; no ESP write started")
    return 0


def list_targets(args):
    _, manifest, _ = prepare_assets(args.lock, args.cache)
    for target in manifest["targets"]:
        aliases = ", ".join(target.get("aliases", []))
        print(
            f"{target['id']}: {target['displayName']} [{target['chipFamily']}]"
            + (f" aliases: {aliases}" if aliases else "")
        )
    return 0


def build_parser():
    parser = argparse.ArgumentParser(
        description="Stage and launch pinned Marauder firmware with the Flipper's on-device ESP flasher"
    )
    parser.add_argument("--lock", type=Path, default=DEFAULT_LOCK_PATH)
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE_ROOT)
    subparsers = parser.add_subparsers(dest="command", required=True)

    prepare = subparsers.add_parser("prepare", help="download and verify pinned assets")
    prepare.set_defaults(
        func=lambda args: (prepare_assets(args.lock, args.cache), 0)[1]
    )

    targets = subparsers.add_parser("targets", help="list pinned board profiles")
    targets.set_defaults(func=list_targets)

    flash = subparsers.add_parser(
        "flash", help="stage verified assets and launch the on-device flasher"
    )
    flash.add_argument("--port")
    flash.add_argument("--target", help="exact manifest target id")
    flash.add_argument("--fap", type=Path, default=ON_DEVICE_FAP)
    flash.set_defaults(func=provision)

    bridge_flash = subparsers.add_parser(
        "bridge-flash",
        help="diagnostic host-esptool path; normal installs use the on-device flasher",
    )
    bridge_flash.add_argument("--port")
    bridge_flash.add_argument("--target", help="exact manifest target id")
    bridge_flash.add_argument("--baud", type=int, default=460800)
    bridge_flash.add_argument("--python", default=sys.executable)
    bridge_flash.set_defaults(func=bridge_provision)
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except MarauderError as error:
        print(f"Marauder provisioning failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
