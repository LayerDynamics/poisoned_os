#!/usr/bin/env python3
"""Run pinned release static gates and emit machine-readable results."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import time
from pathlib import Path

SCHEMA = "poison.release-static-gates/v1"


class GateError(ValueError):
    pass


def load_config(path: Path, profile: str) -> list[dict]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict) or value.get("schema") != SCHEMA or not isinstance(value.get("profiles"), dict):
        raise GateError(f"invalid static gate configuration: {path}")
    gates = value["profiles"].get(profile)
    if not isinstance(gates, list) or not gates:
        raise GateError(f"profile has no gates: {profile}")
    result = []
    seen: set[str] = set()
    for gate in gates:
        if not isinstance(gate, dict) or not isinstance(gate.get("id"), str) or gate["id"] in seen or not isinstance(gate.get("command"), list) or not gate["command"] or any(not isinstance(part, str) or not part for part in gate["command"]):
            raise GateError("gate entries require unique IDs and non-empty string commands")
        seen.add(gate["id"])
        result.append(gate)
    return result


def run_gates(root: Path, gates: list[dict], timeout: int = 1800) -> dict:
    output = []
    for gate in gates:
        command = gate["command"]
        executable = shutil.which(command[0], path=os.environ.get("PATH"))
        started = time.monotonic()
        if executable is None:
            output.append({"id": gate["id"], "command": command, "status": "FAIL", "reason": "tool unavailable", "exitCode": None})
            continue
        try:
            result = subprocess.run(command, cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout, check=False)
            captured = result.stdout
            output.append({"id": gate["id"], "command": command, "status": "PASS" if result.returncode == 0 else "FAIL", "exitCode": result.returncode, "elapsedSeconds": round(time.monotonic() - started, 3), "outputSha256": hashlib.sha256(captured.encode()).hexdigest()})
        except subprocess.TimeoutExpired as error:
            captured = error.stdout or ""
            if isinstance(captured, bytes): captured = captured.decode(errors="replace")
            output.append({"id": gate["id"], "command": command, "status": "FAIL", "reason": "timeout", "exitCode": 124, "outputSha256": hashlib.sha256(captured.encode()).hexdigest()})
    return {"schema": "poison.release-static-results/v1", "result": "PASS" if all(item["status"] == "PASS" for item in output) else "FAIL", "gates": output}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", required=True)
    parser.add_argument("--config", type=Path, default=Path("config/release-static-gates.json"))
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--results", type=Path)
    args = parser.parse_args()
    try:
        results = run_gates(args.root.resolve(), load_config(args.config, args.profile))
    except (OSError, json.JSONDecodeError, GateError) as error:
        results = {"schema": "poison.release-static-results/v1", "result": "FAIL", "error": str(error), "gates": []}
    rendered = json.dumps(results, indent=2, sort_keys=True) + "\n"
    if args.results:
        args.results.parent.mkdir(parents=True, exist_ok=True)
        args.results.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if results["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
