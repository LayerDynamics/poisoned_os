#!/usr/bin/env python3
"""Validate the signed local tool catalog without making network requests."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any

FAMILIES = {"nfc", "lf-rfid", "ibutton", "infrared", "sub-ghz", "gpio", "usb-hid", "ble-hid", "serial", "storage"}
STATUSES = {"foundation", "verified", "unavailable"}
ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]{2,63}$")
CAPABILITY_RE = re.compile(r"^[a-z0-9._-]{1,64}$")
PARAMETER_RE = re.compile(r"^[a-z0-9_]{1,32}$")
DEFAULT_CATALOG = Path(__file__).resolve().parents[2] / "data/poison/catalog/tools.json"


class CatalogError(ValueError):
    """Raised when a catalog violates the bounded local contract."""


def validate_catalog(document: dict[str, Any]) -> dict[str, Any]:
    if document.get("schemaVersion") != 1 or not isinstance(document.get("tools"), list):
        raise CatalogError("catalog schemaVersion must be 1 with a tools array")
    if len(document["tools"]) > 128:
        raise CatalogError("catalog exceeds 128 tools")
    seen: set[str] = set()
    families: set[str] = set()
    for index, tool in enumerate(document["tools"]):
        if not isinstance(tool, dict):
            raise CatalogError(f"tools[{index}] must be an object")
        required = {"id", "family", "status", "purpose", "capabilities", "parameters", "outputs", "sample", "owner", "provenance"}
        missing = required - tool.keys()
        if missing:
            raise CatalogError(f"tools[{index}] missing {sorted(missing)}")
        tool_id = tool["id"]
        if not isinstance(tool_id, str) or not ID_RE.fullmatch(tool_id) or tool_id in seen:
            raise CatalogError(f"tools[{index}] has invalid or duplicate id")
        seen.add(tool_id)
        family = tool["family"]
        if family not in FAMILIES:
            raise CatalogError(f"tools[{index}] has unsupported family")
        families.add(family)
        if tool["status"] not in STATUSES or any(not isinstance(tool[field], str) or not tool[field] for field in ("purpose", "sample", "owner", "provenance")):
            raise CatalogError(f"tools[{index}] has invalid descriptive metadata")
        capabilities = tool["capabilities"]
        if not isinstance(capabilities, list) or len(capabilities) > 16 or any(not isinstance(value, str) or not CAPABILITY_RE.fullmatch(value) for value in capabilities):
            raise CatalogError(f"tools[{index}] has invalid capabilities")
        parameters = tool["parameters"]
        if not isinstance(parameters, list) or len(parameters) > 32:
            raise CatalogError(f"tools[{index}] has invalid parameters")
        parameter_names: set[str] = set()
        for parameter in parameters:
            if not isinstance(parameter, dict) or parameter.get("name") in parameter_names or not isinstance(parameter.get("name"), str) or not PARAMETER_RE.fullmatch(parameter["name"]) or parameter.get("type") not in {"string", "integer", "boolean", "enum"}:
                raise CatalogError(f"tools[{index}] has invalid parameter")
            parameter_names.add(parameter["name"])
        outputs = tool["outputs"]
        if not isinstance(outputs, list) or len(outputs) > 16 or any(not isinstance(value, str) or not value or len(value) > 64 for value in outputs):
            raise CatalogError(f"tools[{index}] has invalid outputs")
    if families != FAMILIES:
        raise CatalogError(f"catalog must account for every family; missing {sorted(FAMILIES - families)}")
    return document


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("catalog", nargs="?", type=Path, default=DEFAULT_CATALOG)
    arguments = parser.parse_args(argv)
    validate_catalog(json.loads(arguments.catalog.read_text(encoding="utf-8")))
    print(f"catalog valid: {arguments.catalog}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
