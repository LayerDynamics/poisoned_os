from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "protocol" / "generate.py"


def load_module():
    spec = importlib.util.spec_from_file_location(
        "poison_protocol_generate", MODULE_PATH
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def fixture_schema() -> dict:
    return {
        "schema": "poison.protocol.snapshot/v1",
        "files": ["fixture.proto"],
        "messages": [
            {
                "name": "Fixture.Item",
                "reservedNames": [],
                "reservedNumbers": [],
                "fields": [
                    {
                        "name": "label",
                        "number": 1,
                        "type": "string",
                        "typeName": None,
                        "cardinality": "optional",
                        "oneof": None,
                    },
                    {
                        "name": "payload",
                        "number": 2,
                        "type": "bytes",
                        "typeName": None,
                        "cardinality": "optional",
                        "oneof": None,
                    },
                    {
                        "name": "tags",
                        "number": 3,
                        "type": "uint32",
                        "typeName": None,
                        "cardinality": "repeated",
                        "oneof": None,
                    },
                ],
            }
        ],
        "enums": [],
    }


class ProtocolCodegenTests(unittest.TestCase):
    def setUp(self) -> None:
        self.module = load_module()
        self.descriptor = fixture_schema()
        self.bounds = {
            "schema": "poison.protocol.bounds/v1",
            "fields": {
                "Fixture.Item.label": {"maxBytes": 32},
                "Fixture.Item.payload": {"maxBytes": 128},
                "Fixture.Item.tags": {"maxCount": 4},
            },
            "messages": {"Fixture.Item": {"maxEncodedBytes": 256}},
            "transport": {
                "chunkBytes": 128,
                "requestQueueDepth": 4,
                "responseQueueDepth": 4,
            },
        }

    def test_bounds_cover_every_variable_field_and_message(self) -> None:
        self.module.validate_bounds(self.descriptor, self.bounds)

        del self.bounds["fields"]["Fixture.Item.payload"]
        with self.assertRaisesRegex(self.module.BoundsError, "Fixture.Item.payload"):
            self.module.validate_bounds(self.descriptor, self.bounds)

    def test_transport_chunk_and_queue_bounds_are_required(self) -> None:
        del self.bounds["transport"]["requestQueueDepth"]
        with self.assertRaisesRegex(self.module.BoundsError, "requestQueueDepth"):
            self.module.validate_bounds(self.descriptor, self.bounds)

    def test_registry_cannot_exceed_nanopb_field_bound(self) -> None:
        options = "Fixture.Item.payload max_size:64\n"
        with self.assertRaisesRegex(self.module.BoundsError, "nanopb max_size 64"):
            self.module.validate_nanopb_bounds(self.bounds, [options])

    def test_schema_snapshot_is_byte_identical_across_output_directories(self) -> None:
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            first_path = Path(first) / "snapshot.json"
            second_path = Path(second) / "snapshot.json"
            self.module.write_schema_snapshot(self.descriptor, first_path)
            self.module.write_schema_snapshot(self.descriptor, second_path)
            self.assertEqual(first_path.read_bytes(), second_path.read_bytes())

    def test_proto_discovery_is_sorted_and_excludes_options(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "z.proto").write_text('syntax = "proto3";\n', encoding="utf-8")
            (root / "a.proto").write_text('syntax = "proto3";\n', encoding="utf-8")
            (root / "a.options").write_text("ignored\n", encoding="utf-8")
            self.assertEqual(
                [path.name for path in self.module.discover_proto_files(root)],
                ["a.proto", "z.proto"],
            )

    def test_full_generation_is_identical_across_output_directories(self) -> None:
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            first_path = Path(first)
            second_path = Path(second)
            self.module.generate_all(ROOT, first_path)
            self.module.generate_all(ROOT, second_path)
            self.assertEqual(
                self.module.digest_tree(first_path),
                self.module.digest_tree(second_path),
            )
            self.assertTrue(list((first_path / "c").glob("*.pb.c")))
            self.assertTrue(list((first_path / "python").glob("*_pb2.py")))
            self.assertTrue(list((first_path / "typescript").glob("*_pb.ts")))
            self.assertTrue(list((first_path / "rust").glob("*.rs")))

    def test_digest_tree_ignores_interpreter_caches(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "python/__pycache__").mkdir(parents=True)
            (root / "python/binding.py").write_text("generated\n", encoding="utf-8")
            (root / "python/__pycache__/binding.cpython-314.pyc").write_bytes(b"cache")
            self.assertEqual(
                list(self.module.digest_tree(root)),
                ["python/binding.py"],
            )

    def test_consumer_binding_sync_uses_canonical_generated_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            generated = root / "generated" / "protocol"
            (generated / "rust").mkdir(parents=True)
            (generated / "typescript").mkdir(parents=True)
            (generated / "rust" / "pb_poison.rs").write_bytes(b"rust-canonical\n")
            for source_name, _ in self.module.DASHBOARD_BINDINGS:
                (generated / "typescript" / source_name).write_bytes(
                    f"typescript:{source_name}\n".encode()
                )

            self.module.sync_consumer_bindings(root, generated)

            for destination_name in self.module.BRIDGE_BINDINGS:
                self.assertEqual(
                    (root / "bridge/src/generated" / destination_name).read_bytes(),
                    b"rust-canonical\n",
                )
            for source_name, destination_name in self.module.DASHBOARD_BINDINGS:
                self.assertEqual(
                    (root / "dashboard/src/generated" / destination_name).read_bytes(),
                    f"typescript:{source_name}\n".encode(),
                )

    def test_fbt_proto_runs_generation_staleness_and_compatibility_checks(self) -> None:
        assets = (ROOT / "assets" / "SConscript").read_text(encoding="utf-8")
        builder = (ROOT / "scripts" / "fbt_tools" / "fbt_assets.py").read_text(
            encoding="utf-8"
        )
        workflow = (ROOT / ".github" / "workflows" / "provenance.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("GenerateProtocolBindings", assets)
        self.assertIn(
            'if assetsenv["IS_BASE_FIRMWARE"]:\n    protocol_governance', assets
        )
        self.assertIn("PROTOCOL_GENERATOR", builder)
        self.assertIn("PROTOCOL_CHECKER", builder)
        self.assertIn("PROTOCOL_COMPATIBILITY", builder)
        self.assertIn("pnpm install --dir tools/protocol --frozen-lockfile", workflow)
        self.assertIn("cargo fetch --locked", workflow)
        self.assertIn("./fbt proto", workflow)

    def test_session_bindings_are_exact_copies_for_dashboard_and_bridge(self) -> None:
        self.assertEqual(
            (ROOT / "dashboard/src/generated/poison-session.ts").read_bytes(),
            (ROOT / "generated/protocol/typescript/poison_session_pb.ts").read_bytes(),
        )
        self.assertEqual(
            (ROOT / "bridge/src/generated/poison_session.rs").read_bytes(),
            (ROOT / "generated/protocol/rust/pb_poison.rs").read_bytes(),
        )

    def test_session_control_messages_are_registered_in_main_dispatch(self) -> None:
        flipper = (ROOT / "assets/protobuf/flipper.proto").read_text(encoding="utf-8")
        for declaration in (
            ".PB_Poison.ChannelOpen poison_channel_open = 108;",
            ".PB_Poison.ChannelOpened poison_channel_opened = 109;",
            ".PB_Poison.CreditUpdate poison_credit_update = 110;",
            ".PB_Poison.CancelRequest poison_cancel_request = 111;",
            ".PB_Poison.Cancelled poison_cancelled = 112;",
            ".PB_Poison.ResumeRequest poison_resume_request = 113;",
            ".PB_Poison.ResumeResponse poison_resume_response = 114;",
        ):
            self.assertIn(declaration, flipper)

    def test_file_and_evidence_bindings_are_exact_dashboard_copies(self) -> None:
        for generated_name, dashboard_name in (
            ("poison_files_pb.ts", "poison-files.ts"),
            ("poison_evidence_pb.ts", "poison-evidence.ts"),
        ):
            self.assertEqual(
                (ROOT / "dashboard/src/generated" / dashboard_name).read_bytes(),
                (ROOT / "generated/protocol/typescript" / generated_name).read_bytes(),
            )


if __name__ == "__main__":
    unittest.main()
