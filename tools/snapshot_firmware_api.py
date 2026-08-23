#!/usr/bin/env python3
"""Create or verify the deterministic PoisonedOS F7 firmware API lock."""

from __future__ import annotations

import argparse
import csv
from io import StringIO
from pathlib import Path
import re
import sys
from typing import Iterable


LOCK_FIELDS = (
    "api_version",
    "kind",
    "status",
    "name",
    "name_hash",
    "signature",
    "source_owner",
    "compatibility",
)
SYMBOL_KINDS = {"Function", "Variable"}
VALID_STATUSES = {"+", "-"}
IDENTIFIER = re.compile(r"[A-Za-z_]\w*\Z")
TOKEN = re.compile(rb"\b[A-Za-z_][A-Za-z0-9_]*\b")


class FirmwareApiSnapshotError(ValueError):
    """Raised when the generated API table cannot form a safe lock."""


def gnu_sym_hash(name: str) -> int:
    """Return the 32-bit GNU hash used by the firmware API resolver."""

    value = 0x1505
    for character in name:
        value = ((value << 5) + value + ord(character)) & 0xFFFFFFFF
    return value


def load_api(path: Path) -> list[dict[str, str]]:
    try:
        with path.open(encoding="utf-8", newline="") as stream:
            reader = csv.DictReader(stream)
            if tuple(reader.fieldnames or ()) != (
                "entry",
                "status",
                "name",
                "type",
                "params",
            ):
                raise FirmwareApiSnapshotError("unexpected API table columns")
            rows = list(reader)
    except (OSError, UnicodeError, csv.Error) as error:
        raise FirmwareApiSnapshotError(f"cannot read API table: {error}") from error
    if not rows:
        raise FirmwareApiSnapshotError("API table is empty")
    return rows


def api_version(rows: Iterable[dict[str, str]]) -> str:
    versions = [row for row in rows if row["entry"] == "Version"]
    if len(versions) != 1:
        raise FirmwareApiSnapshotError("API table must contain exactly one version")
    version = versions[0]
    if version["status"] != "+" or re.fullmatch(r"\d+\.\d+", version["name"]) is None:
        raise FirmwareApiSnapshotError("API version must be approved and numeric")
    return version["name"]


def source_index(root: Path, rows: Iterable[dict[str, str]]) -> dict[str, set[str]]:
    index: dict[str, set[str]] = {}
    for row in rows:
        if row["entry"] != "Header":
            continue
        if row["status"] not in VALID_STATUSES:
            raise FirmwareApiSnapshotError(f"pending API entry: {row['name']}")
        header = row["name"]
        source = root / header
        try:
            content = source.read_bytes()
        except OSError as error:
            raise FirmwareApiSnapshotError(
                f"cannot read API header {header}: {error}"
            ) from error
        for token in set(TOKEN.findall(content)):
            index.setdefault(token.decode("ascii"), set()).add(header)
    return index


def signature(row: dict[str, str]) -> str:
    if row["entry"] == "Function":
        return f"{row['type']} {row['name']}({row['params']})"
    return f"{row['type']} {row['name']}"


def create_rows(root: Path, api_path: Path) -> list[dict[str, str]]:
    rows = load_api(api_path)
    version = api_version(rows)
    owners = source_index(root, rows)
    allow_transitive_owner = (root / "provenance" / "baseline.lock.json").is_file()
    snapshot: list[dict[str, str]] = []
    names: set[str] = set()
    hashes: dict[int, str] = {}
    for row in rows:
        kind = row["entry"]
        if kind not in SYMBOL_KINDS:
            continue
        name = row["name"]
        status = row["status"]
        if status not in VALID_STATUSES:
            raise FirmwareApiSnapshotError(f"pending API entry: {name}")
        if IDENTIFIER.fullmatch(name) is None:
            raise FirmwareApiSnapshotError(f"invalid API symbol name: {name}")
        if name in names:
            raise FirmwareApiSnapshotError(f"duplicate API symbol: {name}")
        names.add(name)
        name_hash = gnu_sym_hash(name)
        if previous := hashes.get(name_hash):
            raise FirmwareApiSnapshotError(f"API hash collision: {previous} and {name}")
        hashes[name_hash] = name
        source_owners = sorted(owners.get(name, ()))
        if not source_owners:
            if not allow_transitive_owner:
                raise FirmwareApiSnapshotError(f"source owner not found: {name}")
            source_owners = ["@sdk/toolchain-or-transitive-runtime"]
        snapshot.append(
            {
                "api_version": version,
                "kind": kind,
                "status": status,
                "name": name,
                "name_hash": f"0x{name_hash:08x}",
                "signature": signature(row),
                "source_owner": ";".join(source_owners),
                "compatibility": "supported" if status == "+" else "disabled",
            }
        )
    if not snapshot:
        raise FirmwareApiSnapshotError("API table contains no symbols")
    return sorted(snapshot, key=lambda row: (row["kind"], row["name"]))


def render(root: Path, api_path: Path) -> str:
    stream = StringIO(newline="")
    writer = csv.DictWriter(stream, fieldnames=LOCK_FIELDS, lineterminator="\n")
    writer.writeheader()
    writer.writerows(create_rows(root, api_path))
    return stream.getvalue()


def parse_arguments() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--api", type=Path, default=root / "targets/f7/api_symbols.csv")
    parser.add_argument(
        "--output",
        type=Path,
        default=root / "provenance/firmware-api.lock.csv",
    )
    parser.add_argument("--check", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        expected = render(arguments.root.resolve(), arguments.api.resolve())
        if arguments.check:
            try:
                actual = arguments.output.read_text(encoding="utf-8")
            except (OSError, UnicodeError) as error:
                raise FirmwareApiSnapshotError(
                    f"cannot read firmware API snapshot: {error}"
                ) from error
            if actual != expected:
                raise FirmwareApiSnapshotError("firmware API snapshot is stale")
            print("firmware API snapshot verified")
        else:
            arguments.output.parent.mkdir(parents=True, exist_ok=True)
            arguments.output.write_text(expected, encoding="utf-8", newline="")
            print(
                f"firmware API snapshot wrote {len(expected.splitlines()) - 1} symbols"
            )
    except (OSError, FirmwareApiSnapshotError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
