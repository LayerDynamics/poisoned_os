"""Physical production-firmware, storage, boot, and recovery checks."""

from __future__ import annotations

import statistics
import sys
import time


def _await_device(context, device) -> str:
    port = context.wait_for_port(device)
    context.run(
        f"await {device.role} CLI",
        [
            sys.executable,
            "scripts/testops.py",
            "-p",
            port,
            "-t",
            str(context.timeout),
            "await_flipper",
        ],
        timeout=context.timeout + 30,
    )
    return port


def _probe(
    context, device, port: str, label: str, runtime_idle_seconds: int | None = None
) -> None:
    evidence = context.results_dir / f"{device.role}-{label}-probe.json"
    argv = [
        sys.executable,
        "scripts/testops.py",
        "-p",
        port,
        "-t",
        str(context.timeout),
        "probe_baseline",
        "--role",
        device.role,
        "--sd-sentinel",
        device.sd_fixture.sentinel_path,
        "--sd-sha256",
        device.sd_fixture.sha256,
        "--evidence",
        str(evidence),
    ]
    if runtime_idle_seconds is not None:
        argv.extend(("--runtime-idle-seconds", str(runtime_idle_seconds)))
    context.run(
        f"probe {device.role} {label}",
        argv,
        timeout=context.timeout + (runtime_idle_seconds or 0) + 30,
    )


def _install_production(context, device, port: str) -> str:
    context.run(
        f"install production firmware on {device.role}",
        ["./fbt", "flash_usb", "FORCE=1", f"FLIP_PORT={port}"],
        timeout=900,
    )
    context.wait_for_disconnect(device)
    return _await_device(context, device)


def _measure_reboot(context, device, port: str, attempt: int) -> tuple[str, float]:
    context.run(
        f"reboot {device.role} {attempt}",
        [sys.executable, "scripts/power.py", "-p", port, "reboot"],
        timeout=60,
    )
    disconnected_at = context.wait_for_disconnect(device)
    port = _await_device(context, device)
    return port, round(time.monotonic() - disconnected_at, 3)


def _recover_over_swd(context, device, port: str) -> str:
    recovery = device.recovery
    if recovery is None:
        raise ValueError("recovery role has no SWD recovery configuration")
    context.run(
        "enter recovery DFU",
        [sys.executable, "scripts/power.py", "-p", port, "reboot2dfu"],
        timeout=60,
    )
    context.wait_for_disconnect(device)
    context.wait_for_dfu(device)
    context.run(
        "recover production firmware over SWD",
        [
            "./fbt",
            "flash",
            "FORCE=1",
            f"SWD_TRANSPORT={recovery.transport}",
            f"SWD_TRANSPORT_SERIAL={recovery.probe_serial}",
        ],
        timeout=900,
    )
    context.power(device, "cycle")
    port = _await_device(context, device)
    _probe(context, device, port, "recovered")
    return port


def run(context, devices) -> None:
    context.require_program("uhubctl")
    context.require_program("dfu-util")
    context.run(
        "build production baseline",
        ["./fbt", "firmware_all", "updater_all", "resources"],
        timeout=1800,
    )
    ports = context.discover_ports(devices)
    boot_samples: dict[str, list[float]] = {}

    for role, device in devices.items():
        port = _install_production(context, device, ports[role])
        _probe(context, device, port, "installed")

        context.power(device, "cycle")
        port = _await_device(context, device)
        _probe(context, device, port, "power-cycle")

        boot_samples[role] = []
        reboot_count = 5 if role == "test" else 1
        for attempt in range(1, reboot_count + 1):
            port, elapsed = _measure_reboot(context, device, port, attempt)
            boot_samples[role].append(elapsed)

        if role == "test":
            _probe(context, device, port, "idle-runtime", runtime_idle_seconds=60)

        if role == "recovery":
            _recover_over_swd(context, device, port)

    context.observations["bootMetrics"] = {
        role: {
            "samplesSeconds": samples,
            "medianSeconds": round(statistics.median(samples), 3),
            "rangeSeconds": [min(samples), max(samples)],
        }
        for role, samples in boot_samples.items()
    }
