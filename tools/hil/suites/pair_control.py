"""Run the physical pairing/control preflight on the real test device."""

from __future__ import annotations

import json
import os
import re
import sys


BOARD_ID = re.compile(r"^[a-z][a-z0-9-]{0,62}$")


def run(context, devices) -> None:
    test_device = devices.get("test")
    if test_device is None:
        raise RuntimeError("pair-control requires a test device")
    board_id = os.environ.get("POISON_HIL_WIFI_BOARD_ID", "")
    if not BOARD_ID.fullmatch(board_id):
        raise RuntimeError("pair-control requires an explicit POISON_HIL_WIFI_BOARD_ID")
    try:
        boards = json.loads(os.environ.get("POISON_WIFI_BOARDS", ""))
    except json.JSONDecodeError as error:
        raise RuntimeError("pair-control requires POISON_WIFI_BOARDS as a JSON object") from error
    if not isinstance(boards, dict) or not isinstance(boards.get(board_id), str):
        raise RuntimeError("POISON_HIL_WIFI_BOARD_ID must name an address in POISON_WIFI_BOARDS")
    context.run("verify dashboard web runtime", ["pnpm", "--dir", "dashboard", "verify"], timeout=600)
    context.run("build safe sample FAP", ["./fbt", "fap_poison_safe_sample"], timeout=900)
    port = context.discover_ports({"test": test_device})["test"]
    context.run("flash pair-control candidate", ["./fbt", "flash_usb", "FORCE=1", f"FLIP_PORT={port}"], timeout=900)
    context.wait_for_disconnect(test_device)
    port = context.wait_for_port(test_device)
    context.run("verify paired device CLI", [sys.executable, "scripts/testops.py", "-p", port, "await_flipper"], timeout=context.timeout + 30)
    context.run(
        "run physical Wi-Fi pair-control workflow",
        ["pnpm", "--dir", "dashboard", "e2e", "--grep", "@physical-pair-control"],
        env={"POISON_HIL_WIFI_BOARD_ID": board_id},
        timeout=max(180, context.timeout * 2),
    )
    context.observations.update({
        "transportRoutes": ["http", "https", "ws", "wss", "wifi", "tcp-uart"],
        "wifiBoardId": board_id,
        "candidate": "poison_safe_sample",
        "mockTransport": False,
    })
