#!/usr/bin/env python3
"""Record a local staged release promotion after health verification."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

CHANNELS = ["internal", "developer", "beta", "stable"]


def promote(manifest: Path, health: Path, output: Path, expected_channel: str) -> dict:
    release = json.loads(manifest.read_text(encoding="utf-8")); report = json.loads(health.read_text(encoding="utf-8"))
    if release.get("schema") != "poison.release-manifest/v1": raise ValueError("invalid release manifest")
    if expected_channel not in CHANNELS or release.get("channel") != expected_channel: raise ValueError("channel mismatch")
    if report.get("result") != "PASS": raise ValueError("rollout health is not PASS")
    promotion = {"schema": "poison.release-promotion/v1", "version": release["version"], "channel": expected_channel, "manifestSha256": hashlib.sha256(manifest.read_bytes()).hexdigest(), "healthSha256": hashlib.sha256(health.read_bytes()).hexdigest()}
    output.parent.mkdir(parents=True, exist_ok=True); output.write_text(json.dumps(promotion, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return promotion


def main() -> int:
    parser = argparse.ArgumentParser(); parser.add_argument("manifest", type=Path); parser.add_argument("health", type=Path); parser.add_argument("--output", type=Path, required=True); parser.add_argument("--channel", required=True)
    args = parser.parse_args()
    try: promote(args.manifest, args.health, args.output, args.channel); return 0
    except (OSError, ValueError, json.JSONDecodeError) as error: parser.error(str(error)); return 2


if __name__ == "__main__": raise SystemExit(main())
