#!/usr/bin/env python3
"""Validate the explicit V1 browser, OS, and transport support matrix."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
from typing import Any


class SupportedClientError(ValueError):
    """Raised when a client matrix violates support or routing policy."""


SCHEMA = "poison.clients/v1"
ROUTES = {"web-runtime"}
STATUSES = {"supported", "planned", "unsupported"}
ID_PATTERN = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")


def load_config(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise SupportedClientError(f"cannot read client matrix: {error}") from error
    if not isinstance(value, dict):
        raise SupportedClientError("client matrix root must be an object")
    return value


def _text(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise SupportedClientError(f"{field} must be a non-empty string")
    return value


def validate_config(config: dict[str, Any]) -> tuple[dict[str, Any], ...]:
    if config.get("schema") != SCHEMA:
        raise SupportedClientError(f"schema must be {SCHEMA}")
    if config.get("sessionContract") != "poison.rpc/v2":
        raise SupportedClientError("sessionContract must be poison.rpc/v2")
    if config.get("defaultRoute") not in ROUTES:
        raise SupportedClientError("defaultRoute is invalid")
    if config.get("defaultRoute") != "web-runtime":
        raise SupportedClientError("dashboard defaultRoute must be web-runtime")
    clients = config.get("clients")
    if not isinstance(clients, list) or not clients:
        raise SupportedClientError("clients must be a non-empty array")
    seen: set[str] = set()
    validated: list[dict[str, Any]] = []
    for index, client in enumerate(clients):
        prefix = f"clients[{index}]"
        if not isinstance(client, dict):
            raise SupportedClientError(f"{prefix} must be an object")
        identifier = _text(client.get("id"), f"{prefix}.id")
        if not ID_PATTERN.fullmatch(identifier):
            raise SupportedClientError(f"{prefix}.id is invalid")
        if identifier in seen:
            raise SupportedClientError(f"duplicate client id: {identifier}")
        seen.add(identifier)
        for field in ("os", "osVersionFloor", "architecture", "browser", "browserVersionFloor", "installation"):
            _text(client.get(field), f"{prefix}.{field}")
        status = client.get("status")
        if status not in STATUSES:
            raise SupportedClientError(f"{prefix}.status is invalid")
        routes = client.get("routes")
        if not isinstance(routes, list) or not routes or any(route not in ROUTES for route in routes):
            raise SupportedClientError(f"{prefix}.routes must contain valid routes")
        if len(routes) != len(set(routes)):
            raise SupportedClientError(f"{prefix}.routes must be unique")
        if routes != ["web-runtime"]:
            raise SupportedClientError(f"{prefix}.routes must use only web-runtime")
        if status == "supported":
            evidence = client.get("hardwareEvidence")
            if not isinstance(evidence, dict):
                raise SupportedClientError(f"supported row {identifier} lacks hardwareEvidence")
            for field in ("hostId", "deviceSerial", "route", "command", "digest"):
                _text(evidence.get(field), f"{prefix}.hardwareEvidence.{field}")
            if evidence["route"] not in routes:
                raise SupportedClientError(f"supported row {identifier} evidence route is not listed")
            if "placeholder" in json.dumps(evidence).lower() or "example" in json.dumps(evidence).lower():
                raise SupportedClientError(f"supported row {identifier} contains placeholder evidence")
        elif client.get("hardwareEvidence") is not None:
            raise SupportedClientError(f"non-supported row {identifier} cannot carry hardwareEvidence")
        validated.append(client)
    return tuple(validated)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        clients = validate_config(load_config(arguments.config.resolve()))
    except (SupportedClientError, OSError) as error:
        print(f"supported-client verification failed: {error}", file=sys.stderr)
        return 1
    supported = sum(client["status"] == "supported" for client in clients)
    print(f"supported-client verification passed: {len(clients)} rows, {supported} supported with physical evidence")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
