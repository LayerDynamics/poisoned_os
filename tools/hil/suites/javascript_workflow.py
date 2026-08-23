"""Exercise the JavaScript dashboard/device workflow on physical hardware."""

from __future__ import annotations

import os
import json
import re

from run_suite import HilError


BOARD_ID = re.compile(r"^[a-z][a-z0-9-]{0,62}$")


def run(context, devices) -> None:
    test_device = devices.get("test")
    if test_device is None:
        raise HilError("javascript-workflow requires the test device")
    board_id = os.environ.get("POISON_HIL_WIFI_BOARD_ID", "")
    if not BOARD_ID.fullmatch(board_id):
        raise HilError("javascript-workflow requires an explicit POISON_HIL_WIFI_BOARD_ID")
    try:
        boards = json.loads(os.environ.get("POISON_WIFI_BOARDS", ""))
    except json.JSONDecodeError as error:
        raise HilError("javascript-workflow requires POISON_WIFI_BOARDS as a JSON object") from error
    if not isinstance(boards, dict) or not isinstance(boards.get(board_id), str):
        raise HilError("POISON_HIL_WIFI_BOARD_ID must name an address in POISON_WIFI_BOARDS")
    port = context.discover_ports({"test": test_device})["test"]
    context.run(
        "verify dashboard JavaScript workflow",
        ["pnpm", "--dir", "dashboard", "verify"],
        timeout=600,
    )
    context.run(
        "run authenticated physical JavaScript workflow",
        ["pnpm", "--dir", "dashboard", "e2e", "--grep", "@physical-javascript"],
        env={
            "POISON_HIL_WIFI_BOARD_ID": board_id,
        },
        timeout=max(360, context.timeout * 3),
    )
    context.observations.update(
        {
            "javascriptWorkflowDevice": "test",
            "javascriptWorkflowPort": port,
            "mockTransport": False,
            "wifiBoardId": board_id,
            "transportRoute": "browser-http-ws-node-wifi-tcp-uart-rpc",
        }
    )
