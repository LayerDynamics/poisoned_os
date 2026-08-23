#!/usr/bin/env python3
"""Run the executable V1 adversarial security matrix locally."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess

MATRIX = Path("docs/security/v1-security-test-matrix.json")


def run_matrix(root: Path, matrix_path: Path = MATRIX, timeout_seconds: int = 120) -> dict:
    document = json.loads(matrix_path.read_text(encoding="utf-8"))
    if document.get("schema") != "poison.security-matrix/v1": raise ValueError("invalid security matrix schema")
    checks = document.get("checks")
    if not isinstance(checks, list) or not checks: raise ValueError("security matrix has no checks")
    results = []
    for check in checks:
        identifier = check.get("id") if isinstance(check, dict) else None
        command = check.get("command") if isinstance(check, dict) else None
        owner = check.get("owner") if isinstance(check, dict) else None
        if not isinstance(identifier, str) or not identifier or not isinstance(owner, str) or not owner or not isinstance(command, list) or not all(isinstance(item, str) for item in command):
            results.append({"id": identifier or "invalid", "status": "FAIL", "reason": "unowned or invalid check"}); continue
        try:
            completed = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=timeout_seconds, check=False)
            output = completed.stdout + completed.stderr
            results.append({"id": identifier, "owner": owner, "status": "PASS" if completed.returncode == 0 else "FAIL", "exitCode": completed.returncode, "outputSha256": hashlib.sha256(output.encode()).hexdigest()})
        except subprocess.TimeoutExpired:
            results.append({"id": identifier, "owner": owner, "status": "FAIL", "reason": "timeout"})
    findings = document.get("findings", [])
    open_high = [finding for finding in findings if isinstance(finding, dict) and finding.get("status") == "open" and finding.get("severity") in {"critical", "high"}]
    result = "PASS" if not open_high and results and all(item["status"] == "PASS" for item in results) else "FAIL"
    return {"schema": "poison.adversarial-results/v1", "result": result, "checks": results, "openCriticalHigh": len(open_high)}


def main() -> int:
    parser = argparse.ArgumentParser(); parser.add_argument("--release-candidate", action="store_true", required=True); parser.add_argument("--root", type=Path, default=Path.cwd()); parser.add_argument("--matrix", type=Path, default=MATRIX); parser.add_argument("--timeout-seconds", type=int, default=120)
    args = parser.parse_args()
    try: result = run_matrix(args.root, args.matrix, args.timeout_seconds)
    except (OSError, ValueError, json.JSONDecodeError) as error: print(json.dumps({"schema": "poison.adversarial-results/v1", "result": "FAIL", "error": str(error)})); return 1
    print(json.dumps(result, indent=2, sort_keys=True)); return 0 if result["result"] == "PASS" else 1


if __name__ == "__main__": raise SystemExit(main())
