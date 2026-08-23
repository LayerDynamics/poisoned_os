#!/usr/bin/env python3
"""Create deterministic platform-labelled release archives."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import zipfile

SUPPORTED = {"macos-arm64", "macos-x64", "windows-x64", "linux-x64"}


def package(source: Path, output_dir: Path, platforms: list[str]) -> dict[str, str]:
    if not source.is_file(): raise ValueError("source archive is missing")
    if not platforms or any(platform not in SUPPORTED for platform in platforms): raise ValueError("unsupported platform")
    source_bytes = source.read_bytes(); output_dir.mkdir(parents=True, exist_ok=True); result = {}
    for platform in sorted(set(platforms)):
        destination = output_dir / f"poisonedos-{platform}.zip"
        with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_STORED) as archive:
            info = zipfile.ZipInfo("release.zip", date_time=(1980, 1, 1, 0, 0, 0)); info.external_attr = 0o100644 << 16; archive.writestr(info, source_bytes)
            info = zipfile.ZipInfo("platform.txt", date_time=(1980, 1, 1, 0, 0, 0)); info.external_attr = 0o100644 << 16; archive.writestr(info, (platform + "\n").encode())
        result[platform] = hashlib.sha256(destination.read_bytes()).hexdigest()
    return result


def main() -> int:
    parser = argparse.ArgumentParser(); parser.add_argument("--source", type=Path, required=True); parser.add_argument("--output-dir", type=Path, required=True); parser.add_argument("platform", nargs="+")
    args = parser.parse_args()
    try: print(package(args.source, args.output_dir, args.platform)); return 0
    except (OSError, ValueError) as error: parser.error(str(error)); return 2


if __name__ == "__main__": raise SystemExit(main())
