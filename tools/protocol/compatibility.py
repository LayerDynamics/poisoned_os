#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def _index(records: list[dict]) -> dict[str, dict]:
    return {record["name"]: record for record in records}


def _member_errors(kind: str, owner: str, previous: dict, current: dict) -> list[str]:
    errors = []
    previous_by_name = _index(previous)
    current_by_name = _index(current)
    previous_by_number = {member["number"]: member for member in previous}
    current_by_number = {member["number"]: member for member in current}

    for name, old in previous_by_name.items():
        new = current_by_name.get(name)
        if new is None:
            errors.append(f"removed {kind} {owner}.{name} (tag {old['number']})")
        elif new["number"] != old["number"]:
            errors.append(
                f"renumbered {kind} {owner}.{name} from {old['number']} to {new['number']}"
            )

    for number, old in previous_by_number.items():
        new = current_by_number.get(number)
        if new is not None and new["name"] != old["name"]:
            errors.append(
                f"reused tag {owner}.{number}: {old['name']} -> {new['name']}"
            )
    return errors


def compatibility_errors(previous: dict, current: dict) -> list[str]:
    errors = []
    previous_messages = _index(previous.get("messages", []))
    current_messages = _index(current.get("messages", []))
    for name, old_message in previous_messages.items():
        new_message = current_messages.get(name)
        if new_message is None:
            errors.append(f"removed message {name}")
            continue
        errors.extend(
            _member_errors(
                "field",
                name,
                old_message.get("fields", []),
                new_message.get("fields", []),
            )
        )
        old_fields = _index(old_message.get("fields", []))
        for field in new_message.get("fields", []):
            if field["number"] in old_message.get("reservedNumbers", []):
                errors.append(
                    f"field {name}.{field['name']} uses previously reserved tag {field['number']}"
                )
            if field["name"] in old_message.get("reservedNames", []):
                errors.append(
                    f"field {name}.{field['name']} uses previously reserved name"
                )
            old_field = old_fields.get(field["name"])
            if old_field is not None:
                for property_name in ("type", "typeName", "cardinality", "oneof"):
                    if old_field.get(property_name) != field.get(property_name):
                        errors.append(
                            f"changed field {name}.{field['name']} {property_name}"
                        )

    previous_enums = _index(previous.get("enums", []))
    current_enums = _index(current.get("enums", []))
    for name, old_enum in previous_enums.items():
        new_enum = current_enums.get(name)
        if new_enum is None:
            errors.append(f"removed enum {name}")
            continue
        errors.extend(
            _member_errors(
                "enum value",
                name,
                old_enum.get("values", []),
                new_enum.get("values", []),
            )
        )
        for value in new_enum.get("values", []):
            if value["number"] in old_enum.get("reservedNumbers", []):
                errors.append(
                    f"enum value {name}.{value['name']} uses previously reserved number {value['number']}"
                )
            if value["name"] in old_enum.get("reservedNames", []):
                errors.append(
                    f"enum value {name}.{value['name']} uses previously reserved name"
                )

    return sorted(set(errors))


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Check protocol wire compatibility")
    parser.add_argument("--against", required=True, type=Path)
    parser.add_argument(
        "--current",
        type=Path,
        default=ROOT / "generated" / "protocol" / "schema.snapshot.json",
    )
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    previous = json.loads(args.against.read_text(encoding="utf-8"))
    current = json.loads(args.current.read_text(encoding="utf-8"))
    errors = compatibility_errors(previous, current)
    if errors:
        for error in errors:
            print(error)
        return 1
    print("protocol compatibility passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
