#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def _load_generator():
    module_path = ROOT / "tools" / "protocol" / "generate.py"
    spec = importlib.util.spec_from_file_location(
        "poison_protocol_generate", module_path
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def compare_generated(expected: Path, actual: Path) -> list[str]:
    generator = _load_generator()
    expected_files = generator.digest_tree(expected)
    actual_files = generator.digest_tree(actual)
    errors = []
    for relative in sorted(expected_files.keys() - actual_files.keys()):
        errors.append(f"missing generated file: {relative}")
    for relative in sorted(actual_files.keys() - expected_files.keys()):
        errors.append(f"unexpected generated file: {relative}")
    for relative in sorted(expected_files.keys() & actual_files.keys()):
        if expected_files[relative] != actual_files[relative]:
            errors.append(f"stale generated file: {relative}")
    return errors


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Verify checked-in protocol bindings")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument(
        "--generated", type=Path, default=ROOT / "generated" / "protocol"
    )
    return parser.parse_args()


def main() -> int:
    args = _parse_args()
    generator = _load_generator()
    with tempfile.TemporaryDirectory(prefix="poison-protocol-check-") as temporary:
        expected = Path(temporary)
        generator.generate_all(args.root, expected)
        errors = compare_generated(expected, args.generated)
    if errors:
        for error in errors:
            print(error)
        return 1
    print("generated protocol bindings are current")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
