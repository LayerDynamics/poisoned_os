#!/usr/bin/env python3
from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Iterable

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.rust.toolchain import prepare


ROOT = Path(__file__).resolve().parents[2]
BRIDGE_BINDINGS = (
    "pb_poison.rs",
    "poison_app.rs",
    "poison_profiles.rs",
    "poison_session.rs",
    "poison_workload.rs",
)
DASHBOARD_BINDINGS = (
    ("application_pb.ts", "application_pb.ts"),
    ("desktop_pb.ts", "desktop_pb.ts"),
    ("flipper_pb.ts", "flipper_pb.ts"),
    ("gpio_pb.ts", "gpio_pb.ts"),
    ("gui_pb.ts", "gui_pb.ts"),
    ("poison_app_pb.ts", "poison-app.ts"),
    ("poison_app_pb.ts", "poison_app_pb.ts"),
    ("poison_audit_pb.ts", "poison_audit_pb.ts"),
    ("poison_diagnostics_pb.ts", "poison_diagnostics_pb.ts"),
    ("poison_evidence_pb.ts", "poison-evidence.ts"),
    ("poison_evidence_pb.ts", "poison_evidence_pb.ts"),
    ("poison_files_pb.ts", "poison-files.ts"),
    ("poison_files_pb.ts", "poison_files_pb.ts"),
    ("poison_packages_pb.ts", "poison-packages.ts"),
    ("poison_packages_pb.ts", "poison_packages_pb.ts"),
    ("poison_policy_pb.ts", "poison_policy_pb.ts"),
    ("poison_profiles_pb.ts", "poison-profiles.ts"),
    ("poison_profiles_pb.ts", "poison_profiles_pb.ts"),
    ("poison_session_pb.ts", "poison-session.ts"),
    ("poison_session_pb.ts", "poison_session_pb.ts"),
    ("poison_workload_pb.ts", "poison-workload.ts"),
    ("poison_workload_pb.ts", "poison_workload_pb.ts"),
    ("property_pb.ts", "property_pb.ts"),
    ("storage_pb.ts", "storage_pb.ts"),
    ("system_pb.ts", "system_pb.ts"),
)
FIELD_TYPES = {
    1: "double",
    2: "float",
    3: "int64",
    4: "uint64",
    5: "int32",
    6: "fixed64",
    7: "fixed32",
    8: "bool",
    9: "string",
    10: "group",
    11: "message",
    12: "bytes",
    13: "uint32",
    14: "enum",
    15: "sfixed32",
    16: "sfixed64",
    17: "sint32",
    18: "sint64",
}
CARDINALITIES = {
    1: "optional",
    2: "required",
    3: "repeated",
}
VARIABLE_TYPE_NAMES = {"string", "bytes"}
OPTION_RE = re.compile(
    r"^(?P<field>\S+)\s+(?P<option>max_length|max_size|max_count):(?P<value>\d+)\s*$"
)


class BoundsError(ValueError):
    """Raised when the protocol bounds registry is incomplete or unsafe."""


def discover_proto_files(proto_root: Path) -> list[Path]:
    return sorted(proto_root.glob("*.proto"), key=lambda path: path.name)


def _full_name(package: str, parents: tuple[str, ...], name: str) -> str:
    return ".".join(part for part in (package, *parents, name) if part)


def _message_records(
    package: str,
    messages: Iterable[object],
    parents: tuple[str, ...] = (),
) -> Iterable[tuple[str, object]]:
    for message in messages:
        name = _full_name(package, parents, message.name)
        yield name, message
        yield from _message_records(
            package, message.nested_type, (*parents, message.name)
        )


def _enum_records(
    package: str,
    enums: Iterable[object],
    parents: tuple[str, ...] = (),
) -> Iterable[tuple[str, object]]:
    for enum in enums:
        yield _full_name(package, parents, enum.name), enum


def _all_enum_records(
    package: str,
    messages: Iterable[object],
    enums: Iterable[object],
    parents: tuple[str, ...] = (),
) -> Iterable[tuple[str, object]]:
    yield from _enum_records(package, enums, parents)
    for message in messages:
        message_parents = (*parents, message.name)
        yield from _enum_records(package, message.enum_type, message_parents)
        yield from _all_enum_records(
            package,
            message.nested_type,
            (),
            message_parents,
        )


def _expanded_ranges(ranges: Iterable[object]) -> list[int]:
    return sorted(
        number
        for reserved_range in ranges
        for number in range(reserved_range.start, reserved_range.end)
    )


def schema_snapshot(descriptor_set: object) -> dict:
    if isinstance(descriptor_set, dict):
        return descriptor_set
    messages = []
    enums = []
    for proto in sorted(descriptor_set.file, key=lambda item: item.name):
        for name, message in _message_records(proto.package, proto.message_type):
            oneofs = [oneof.name for oneof in message.oneof_decl]
            fields = []
            for field in sorted(message.field, key=lambda item: item.number):
                fields.append(
                    {
                        "name": field.name,
                        "number": field.number,
                        "type": FIELD_TYPES[field.type],
                        "typeName": field.type_name.removeprefix(".") or None,
                        "cardinality": CARDINALITIES[field.label],
                        "oneof": (
                            oneofs[field.oneof_index]
                            if field.HasField("oneof_index")
                            else None
                        ),
                    }
                )
            messages.append(
                {
                    "name": name,
                    "reservedNames": sorted(message.reserved_name),
                    "reservedNumbers": _expanded_ranges(message.reserved_range),
                    "fields": fields,
                }
            )
        for name, enum in _all_enum_records(
            proto.package, proto.message_type, proto.enum_type
        ):
            enums.append(
                {
                    "name": name,
                    "reservedNames": sorted(enum.reserved_name),
                    "reservedNumbers": _expanded_ranges(enum.reserved_range),
                    "values": [
                        {"name": value.name, "number": value.number}
                        for value in sorted(
                            enum.value, key=lambda item: (item.number, item.name)
                        )
                    ],
                }
            )
    return {
        "schema": "poison.protocol.snapshot/v1",
        "files": sorted(proto.name for proto in descriptor_set.file),
        "messages": sorted(messages, key=lambda item: item["name"]),
        "enums": sorted(enums, key=lambda item: item["name"]),
    }


def write_schema_snapshot(descriptor_set: object, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(schema_snapshot(descriptor_set), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def validate_bounds(descriptor_set: object, registry: dict) -> None:
    if registry.get("schema") != "poison.protocol.bounds/v1":
        raise BoundsError("bounds registry schema must be poison.protocol.bounds/v1")

    fields = registry.get("fields", {})
    messages = registry.get("messages", {})
    snapshot = schema_snapshot(descriptor_set)
    for message in snapshot.get("messages", []):
        message_name = message["name"]
        message_bound = messages.get(message_name, {}).get("maxEncodedBytes")
        if not isinstance(message_bound, int) or message_bound <= 0:
            raise BoundsError(f"missing positive maxEncodedBytes for {message_name}")
        for field in message.get("fields", []):
            if field["type"] not in VARIABLE_TYPE_NAMES and (
                field["cardinality"] != "repeated"
            ):
                continue
            field_name = f"{message_name}.{field['name']}"
            field_bound = fields.get(field_name, {})
            key = "maxCount" if field["cardinality"] == "repeated" else "maxBytes"
            value = field_bound.get(key)
            if not isinstance(value, int) or value <= 0:
                raise BoundsError(f"missing positive {key} for {field_name}")

    transport = registry.get("transport", {})
    for key in ("chunkBytes", "requestQueueDepth", "responseQueueDepth"):
        value = transport.get(key)
        if not isinstance(value, int) or value <= 0:
            raise BoundsError(f"missing positive transport {key}")


def _nanopb_limits(options_documents: Iterable[str]) -> list[tuple[str, str, int]]:
    limits = []
    for document in options_documents:
        for raw_line in document.splitlines():
            line = raw_line.split("//", 1)[0].strip()
            match = OPTION_RE.match(line)
            if match:
                limits.append(
                    (
                        match.group("field"),
                        match.group("option"),
                        int(match.group("value")),
                    )
                )
    return limits


def validate_nanopb_bounds(registry: dict, options_documents: Iterable[str]) -> None:
    limits = _nanopb_limits(options_documents)
    for field_name, bound in registry.get("fields", {}).items():
        for pattern, option, maximum in limits:
            if not fnmatch.fnmatchcase(field_name, pattern):
                continue
            key = "maxCount" if option == "max_count" else "maxBytes"
            value = bound.get(key)
            if isinstance(value, int) and value > maximum:
                raise BoundsError(
                    f"{field_name} bound {value} exceeds nanopb {option} {maximum}"
                )


def _run(args: list[str], *, cwd: Path, env: dict[str, str] | None = None) -> None:
    subprocess.run(args, cwd=cwd, env=env, check=True)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def digest_tree(root: Path) -> dict[str, str]:
    return {
        path.relative_to(root).as_posix(): _sha256(path)
        for path in sorted(root.rglob("*"))
        if path.is_file()
        and "__pycache__" not in path.parts
        and path.suffix not in {".pyc", ".pyo"}
    }


def _load_json_yaml(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BoundsError(f"cannot load bounds registry {path}: {error}") from error
    if not isinstance(value, dict):
        raise BoundsError(f"bounds registry {path} must contain an object")
    return value


def _require_path(path: Path, description: str) -> Path:
    if not path.is_file():
        raise FileNotFoundError(f"missing {description}: {path}")
    return path


def _snapshot_descriptor(
    python: Path, script: Path, descriptor: Path, snapshot: Path, root: Path
) -> None:
    _run(
        [
            str(python),
            str(script),
            "--snapshot-descriptor",
            str(descriptor),
            "--snapshot-output",
            str(snapshot),
        ],
        cwd=root,
    )


def _sync_generated(staging: Path, output: Path) -> None:
    expected = {
        path.relative_to(staging) for path in staging.rglob("*") if path.is_file()
    }
    output.mkdir(parents=True, exist_ok=True)
    managed_roots = ("c", "python", "typescript", "rust")
    for managed_root in managed_roots:
        current_root = output / managed_root
        if current_root.exists():
            for path in sorted(current_root.rglob("*"), reverse=True):
                if path.is_file() and path.relative_to(output) not in expected:
                    path.unlink()
                elif path.is_dir() and not any(path.iterdir()):
                    path.rmdir()
    for relative in sorted(expected):
        destination = output / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(staging / relative, destination)


def sync_consumer_bindings(root: Path, generated: Path) -> None:
    rust_source = _require_path(
        generated / "rust" / "pb_poison.rs", "generated Rust PB_Poison binding"
    )
    bridge_output = root / "bridge" / "src" / "generated"
    bridge_output.mkdir(parents=True, exist_ok=True)
    for destination_name in BRIDGE_BINDINGS:
        shutil.copyfile(rust_source, bridge_output / destination_name)

    dashboard_output = root / "dashboard" / "src" / "generated"
    dashboard_output.mkdir(parents=True, exist_ok=True)
    for source_name, destination_name in DASHBOARD_BINDINGS:
        source = _require_path(
            generated / "typescript" / source_name,
            f"generated TypeScript binding {source_name}",
        )
        shutil.copyfile(source, dashboard_output / destination_name)


def generate_all(root: Path, output: Path) -> None:
    root = root.resolve()
    output = output.resolve()
    proto_root = root / "assets" / "protobuf"
    proto_files = discover_proto_files(proto_root)
    if not proto_files:
        raise FileNotFoundError(f"no canonical .proto files in {proto_root}")

    toolchain = root / "toolchain" / "current" / "bin"
    python = _require_path(toolchain / "python3", "pinned Python")
    protoc = _require_path(toolchain / "protoc", "pinned protoc")
    nanopb = _require_path(
        root / "lib" / "nanopb" / "generator" / "nanopb_generator.py",
        "nanopb generator",
    )
    es_plugin = _require_path(
        root / "tools" / "protocol" / "node_modules" / ".bin" / "protoc-gen-es",
        "pinned TypeScript protoc plugin; run pnpm install --dir tools/protocol --frozen-lockfile",
    )
    cargo_manifest = _require_path(
        root / "tools" / "protocol" / "Cargo.toml", "Rust generator manifest"
    )
    cargo_lock = _require_path(
        root / "tools" / "protocol" / "Cargo.lock", "Rust generator lockfile"
    )
    rust_toolchain = _require_path(
        root / "tools" / "protocol" / "rust-toolchain.toml",
        "pinned Rust toolchain",
    )
    bounds_path = _require_path(
        root / "schemas" / "protocol" / "bounds.yaml", "protocol bounds registry"
    )
    script = Path(__file__).resolve()
    source_names = [path.name for path in proto_files]
    subprocess_env = dict(os.environ)
    subprocess_env["PATH"] = f"{toolchain}{os.pathsep}{subprocess_env.get('PATH', '')}"
    subprocess_env["PROTOC"] = str(protoc)
    prepare(root, subprocess_env)

    with tempfile.TemporaryDirectory(prefix="poison-protocol-") as temporary:
        staging = Path(temporary)
        c_output = staging / "c"
        python_output = staging / "python"
        typescript_output = staging / "typescript"
        rust_output = staging / "rust"
        for path in (c_output, python_output, typescript_output, rust_output):
            path.mkdir(parents=True)

        descriptor = staging / "schema.descriptor.pb"
        snapshot = staging / "schema.snapshot.json"
        _run(
            [
                str(protoc),
                "--include_imports",
                f"--descriptor_set_out={descriptor}",
                *source_names,
            ],
            cwd=proto_root,
            env=subprocess_env,
        )
        _snapshot_descriptor(python, script, descriptor, snapshot, root)
        schema = json.loads(snapshot.read_text(encoding="utf-8"))
        registry = _load_json_yaml(bounds_path)
        validate_bounds(schema, registry)
        options_documents = [
            path.read_text(encoding="utf-8")
            for path in sorted(proto_root.glob("*.options"))
        ]
        validate_nanopb_bounds(registry, options_documents)

        _run(
            [
                str(python),
                str(nanopb),
                "-q",
                "-I.",
                f"-D{c_output}",
                *source_names,
            ],
            cwd=proto_root,
            env=subprocess_env,
        )
        _run(
            [
                str(protoc),
                f"--python_out={python_output}",
                *source_names,
            ],
            cwd=proto_root,
            env=subprocess_env,
        )
        _run(
            [
                str(protoc),
                f"--plugin=protoc-gen-es={es_plugin}",
                f"--es_out={typescript_output}",
                "--es_opt=target=ts",
                *source_names,
            ],
            cwd=proto_root,
            env=subprocess_env,
        )
        _run(
            [
                "cargo",
                "run",
                "--quiet",
                "--locked",
                "--manifest-path",
                str(cargo_manifest),
                "--",
                str(proto_root),
                str(rust_output),
                *source_names,
            ],
            cwd=root,
            env=subprocess_env,
        )
        rustfmt = shutil.which("rustfmt", path=subprocess_env["PATH"])
        if not rustfmt:
            raise FileNotFoundError("rustfmt from the pinned Rust toolchain is missing")
        _run(
            [rustfmt, "--edition", "2024", *map(str, sorted(rust_output.glob("*.rs")))],
            cwd=rust_toolchain.parent,
            env=subprocess_env,
        )

        generated = digest_tree(staging)
        manifest = {
            "schema": "poison.protocol.generated/v1",
            "canonicalProtoRoot": "assets/protobuf",
            "bounds": "schemas/protocol/bounds.yaml",
            "generators": {
                "nanopb": "lib/nanopb/generator/nanopb_generator.py",
                "python": "pinned protoc Python generator",
                "typescript": "@bufbuild/protoc-gen-es@2.14.0",
                "rust": "prost-build@0.14.4",
                "rustfmt": "rustfmt 1.9.0 (Rust 1.96.0)",
            },
            "inputs": {
                path.relative_to(root).as_posix(): _sha256(path)
                for path in [
                    *proto_files,
                    *sorted(proto_root.glob("*.options")),
                    bounds_path,
                    cargo_manifest,
                    cargo_lock,
                    rust_toolchain,
                    root / "tools" / "protocol" / "package.json",
                    root / "tools" / "protocol" / "pnpm-lock.yaml",
                ]
            },
            "outputs": generated,
        }
        (staging / "manifest.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        _sync_generated(staging, output)
        if output == root / "generated" / "protocol":
            sync_consumer_bindings(root, output)


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate PoisonedOS protocol bindings"
    )
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path, default=ROOT / "generated" / "protocol")
    parser.add_argument("--snapshot-descriptor", type=Path)
    parser.add_argument("--snapshot-output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    if args.snapshot_descriptor:
        if not args.snapshot_output:
            raise SystemExit("--snapshot-output is required with --snapshot-descriptor")
        from google.protobuf import descriptor_pb2

        descriptor_set = descriptor_pb2.FileDescriptorSet()
        descriptor_set.ParseFromString(args.snapshot_descriptor.read_bytes())
        write_schema_snapshot(descriptor_set, args.snapshot_output)
        return 0
    if args.snapshot_output:
        raise SystemExit("--snapshot-descriptor is required with --snapshot-output")
    generate_all(args.root, args.output)
    print(f"protocol generation passed: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
