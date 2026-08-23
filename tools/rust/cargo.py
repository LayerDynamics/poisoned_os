#!/usr/bin/env python3
"""Run pinned Cargo and synchronize local toolchain metadata when unconfigured."""

from __future__ import annotations

import subprocess
import sys
import os
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from toolchain import prepare, requested_channel


def _requested_channel(root: Path) -> str:
    return requested_channel(root)


def _cargo(root: Path, environment: dict[str, str] | None = None) -> Path:
    return prepare(root, environment if environment is not None else os.environ.copy())


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    if not sys.argv[1:]:
        raise SystemExit("usage: cargo.py <cargo arguments>")
    environment = os.environ.copy()
    cargo = prepare(root, environment)
    environment["PATH"] = os.pathsep.join((str(cargo.parent), environment.get("PATH", "")))
    return subprocess.run([str(cargo), *sys.argv[1:]], cwd=root, env=environment, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
