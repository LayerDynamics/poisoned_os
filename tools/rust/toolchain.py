"""Select and locally synchronize the repository's pinned Rust toolchain."""

from __future__ import annotations

import json
import os
import re
import subprocess
import tempfile
from pathlib import Path


def requested_channel(root: Path) -> str:
    text = (root / "rust-toolchain.toml").read_text(encoding="utf-8")
    match = re.search(r'channel\s*=\s*"(1\.\d+(?:\.\d+)?)"', text)
    if not match:
        raise RuntimeError("rust-toolchain.toml must pin a numeric Rust channel")
    return match.group(1)


def installed_cargo(root: Path, environment: dict[str, str]) -> Path:
    rustup_home = Path(environment.get("RUSTUP_HOME", str(Path.home() / ".rustup")))
    requested = environment.get("RUSTUP_TOOLCHAIN", requested_channel(root))
    candidates = sorted(rustup_home.glob(f"toolchains/{requested}*/bin/cargo"))
    if not candidates:
        raise RuntimeError(f"installed pinned Cargo is missing for {requested}")
    return candidates[0]


def _metadata_path(root: Path) -> Path:
    return root / "build" / "toolchain-metadata.json"


def sync_metadata(root: Path, environment: dict[str, str], cargo: Path) -> None:
    """Synchronize local compiler metadata without invoking rustup or the network."""

    if environment.get("RUSTUP_TOOLCHAIN") or environment.get("RUSTC"):
        return
    rustc = cargo.with_name("rustc")
    try:
        version = subprocess.run(
            [str(rustc), "--version", "--verbose"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.splitlines()
        targets = subprocess.run(
            [str(rustc), "--print", "target-list"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.splitlines()
    except (OSError, subprocess.CalledProcessError) as error:
        raise RuntimeError(f"cannot read pinned Rust toolchain metadata: {error}") from error
    metadata = {
        "schema": "poison.rust-toolchain-metadata/v1",
        "channel": requested_channel(root),
        "toolchain": cargo.parent.parent.name,
        "cargo": str(cargo),
        "rustcVersion": [line for line in version if line],
        "targets": sorted(target for target in targets if target),
    }
    destination = _metadata_path(root)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", dir=destination.parent, delete=False
    ) as temporary:
        json.dump(metadata, temporary, indent=2, sort_keys=True)
        temporary.write("\n")
        temporary_path = Path(temporary.name)
    temporary_path.replace(destination)


def prepare(root: Path, environment: dict[str, str]) -> Path:
    cargo = installed_cargo(root, environment)
    sync_metadata(root, environment, cargo)
    if not environment.get("RUSTUP_TOOLCHAIN") and not environment.get("RUSTC"):
        environment["RUSTUP_TOOLCHAIN"] = cargo.parent.parent.name
    return cargo
