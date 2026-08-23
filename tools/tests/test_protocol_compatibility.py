from __future__ import annotations

import copy
import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "protocol" / "compatibility.py"


def load_module():
    spec = importlib.util.spec_from_file_location(
        "poison_protocol_compatibility", MODULE_PATH
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def previous_schema() -> dict:
    return {
        "schema": "poison.protocol.snapshot/v1",
        "files": ["fixture.proto"],
        "messages": [
            {
                "name": "Fixture.Item",
                "reservedNames": ["legacy"],
                "reservedNumbers": [9],
                "fields": [
                    {
                        "name": "label",
                        "number": 1,
                        "type": "string",
                        "typeName": None,
                        "cardinality": "optional",
                        "oneof": None,
                    }
                ],
            }
        ],
        "enums": [
            {
                "name": "Fixture.Mode",
                "reservedNames": [],
                "reservedNumbers": [],
                "values": [{"name": "MODE_UNKNOWN", "number": 0}],
            }
        ],
    }


class ProtocolCompatibilityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.module = load_module()
        self.previous = previous_schema()

    def test_optional_field_addition_is_compatible(self) -> None:
        current = copy.deepcopy(self.previous)
        current["messages"][0]["fields"].append(
            {
                "name": "note",
                "number": 2,
                "type": "string",
                "typeName": None,
                "cardinality": "optional",
                "oneof": None,
            }
        )
        self.assertEqual(self.module.compatibility_errors(self.previous, current), [])

    def test_enum_value_addition_is_compatible(self) -> None:
        current = copy.deepcopy(self.previous)
        current["enums"][0]["values"].append({"name": "MODE_ACTIVE", "number": 1})
        self.assertEqual(self.module.compatibility_errors(self.previous, current), [])

    def test_removed_field_is_rejected(self) -> None:
        current = copy.deepcopy(self.previous)
        current["messages"][0]["fields"] = []
        self.assertIn(
            "removed field Fixture.Item.label (tag 1)",
            self.module.compatibility_errors(self.previous, current),
        )

    def test_renumbered_field_is_rejected(self) -> None:
        current = copy.deepcopy(self.previous)
        current["messages"][0]["fields"][0]["number"] = 2
        self.assertIn(
            "renumbered field Fixture.Item.label from 1 to 2",
            self.module.compatibility_errors(self.previous, current),
        )

    def test_field_tag_reuse_is_rejected(self) -> None:
        current = copy.deepcopy(self.previous)
        current["messages"][0]["fields"][0]["name"] = "replacement"
        self.assertIn(
            "reused tag Fixture.Item.1: label -> replacement",
            self.module.compatibility_errors(self.previous, current),
        )

    def test_reserved_field_tag_reuse_is_rejected(self) -> None:
        current = copy.deepcopy(self.previous)
        current["messages"][0]["fields"].append(
            {
                "name": "forbidden",
                "number": 9,
                "type": "uint32",
                "typeName": None,
                "cardinality": "optional",
                "oneof": None,
            }
        )
        self.assertIn(
            "field Fixture.Item.forbidden uses previously reserved tag 9",
            self.module.compatibility_errors(self.previous, current),
        )


if __name__ == "__main__":
    unittest.main()
