#!/usr/bin/env python3
"""Run the bounded, offline release fuzz-target matrix."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

TARGETS = {
    "protocol": ("tests/fuzz/protocol/Cargo.toml", "session_envelope"),
    "packages": ("tests/fuzz/packages/Cargo.toml", "package_manifest"),
    "evidence": ("tests/fuzz/evidence/Cargo.toml", "evidence_manifest"),
    "wasm": ("tests/fuzz/wasm/Cargo.toml", "module_loader"),
}


def run_matrix(root: Path, timeout_seconds: int = 60, runner: str = "tools/rust/cargo.py") -> dict:
    if timeout_seconds < 1 or timeout_seconds > 3600: raise ValueError("timeout must be between 1 and 3600 seconds")
    results = []
    for name, (manifest, target) in TARGETS.items():
        manifest_path = root / manifest
        if not manifest_path.is_file(): results.append({"id": name, "status": "FAIL", "reason": "missing manifest"}); continue
        command = [sys.executable, str(root / runner), "test", "--manifest-path", str(manifest_path), "--offline"]
        try:
            completed = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=timeout_seconds, check=False)
            output = completed.stdout + completed.stderr
            results.append({"id": name, "target": target, "status": "PASS" if completed.returncode == 0 else "FAIL", "exitCode": completed.returncode, "outputSha256": hashlib.sha256(output.encode()).hexdigest()})
        except subprocess.TimeoutExpired:
            results.append({"id": name, "target": target, "status": "FAIL", "reason": "timeout"})
    return {"schema": "poison.fuzz-matrix/v1", "profile": "release", "result": "PASS" if all(item["status"] == "PASS" for item in results) and len(results) == len(TARGETS) else "FAIL", "targets": results}


def main() -> int:
    parser = argparse.ArgumentParser(); parser.add_argument("--profile", choices=["release"], required=True); parser.add_argument("--root", type=Path, default=Path.cwd()); parser.add_argument("--timeout-seconds", type=int, default=60)
    args = parser.parse_args(); result = run_matrix(args.root, args.timeout_seconds); print(json.dumps(result, indent=2, sort_keys=True)); return 0 if result["result"] == "PASS" else 1


if __name__ == "__main__": raise SystemExit(main())
