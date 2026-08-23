"""Build, install, and execute the physical firmware unit-test image."""

from __future__ import annotations

import sys


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


def _flash(context, device, port: str, app_set: str | None) -> str:
    argv = ["./fbt"]
    if app_set:
        argv.append(f"FIRMWARE_APP_SET={app_set}")
    argv.extend(("flash_usb", "FORCE=1", f"FLIP_PORT={port}"))
    context.run(
        f"install {app_set or 'production'} firmware on {device.role}",
        argv,
        timeout=900,
    )
    context.wait_for_disconnect(device)
    return _await_device(context, device)


def _restore_production(context, devices) -> None:
    context.run(
        "rebuild production firmware",
        ["./fbt", "firmware_all", "updater_all", "resources"],
        timeout=1800,
    )
    ports = context.discover_ports(devices)
    for role, device in devices.items():
        _flash(context, device, ports[role], None)


def run(context, devices) -> None:
    ports = context.discover_ports(devices)
    context.run(
        "build firmware unit image",
        [
            "./fbt",
            "FIRMWARE_APP_SET=unit_tests",
            "firmware_all",
            "updater_all",
        ],
        timeout=1800,
    )

    failure = None
    try:
        for role, device in devices.items():
            port = _flash(context, device, ports[role], "unit_tests")
            context.run(
                f"run firmware units on {role}",
                [
                    sys.executable,
                    "scripts/testops.py",
                    "-p",
                    port,
                    "run_units",
                    "--output",
                    str(context.results_dir / f"{role}-unit-tests.txt"),
                ],
                timeout=1800,
            )
    except Exception as error:
        failure = error
    finally:
        _restore_production(context, devices)
    if failure:
        raise failure
