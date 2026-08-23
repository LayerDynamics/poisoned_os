#!/usr/bin/env python3
"""Approve generated pending SDK symbols after an explicit API review."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
import sys


class ApiApprovalError(ValueError):
    """Raised when the generated SDK symbol table is not approvable."""


def approve(path: Path) -> int:
    try:
        with path.open(newline="", encoding="utf-8") as source:
            rows = list(csv.DictReader(source))
    except (OSError, UnicodeError, csv.Error) as error:
        raise ApiApprovalError(f"cannot read API symbol table: {error}") from error
    if not rows or set(rows[0]) != {"entry", "status", "name", "type", "params"}:
        raise ApiApprovalError("API symbol table has an invalid header")
    versions = [row for row in rows if row["entry"] == "Version"]
    if len(versions) != 1 or versions[0]["status"] != "v":
        raise ApiApprovalError("expected exactly one pending Version row")
    pending = [row for row in rows if row["status"] == "?"]
    if not pending:
        raise ApiApprovalError("API symbol table has no pending entries")
    for row in rows:
        if row["status"] == "?":
            row["status"] = "+"
    versions[0]["status"] = "+"
    try:
        with path.open("w", newline="", encoding="utf-8") as destination:
            writer = csv.DictWriter(
                destination,
                fieldnames=("entry", "status", "name", "type", "params"),
                lineterminator="\n",
            )
            writer.writeheader()
            writer.writerows(rows)
    except (OSError, csv.Error) as error:
        raise ApiApprovalError(f"cannot write API symbol table: {error}") from error
    return len(pending)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--api", type=Path, default=Path("targets/f7/api_symbols.csv"))
    arguments = parser.parse_args()
    try:
        count = approve(arguments.api)
    except ApiApprovalError as error:
        print(f"API approval failed: {error}", file=sys.stderr)
        return 1
    print(f"approved {count} generated SDK entries in {arguments.api}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
