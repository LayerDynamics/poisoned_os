#!/usr/bin/env python3
"""Evaluate versioned rollout-health evidence without network access."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any

SCHEMA = "poison.rollout-health/v1"
DEFAULT_CANDIDATE = Path("artifacts/release-evidence/rollout-health-candidate.json")


def evaluate(document: dict[str, Any]) -> dict[str, Any]:
    failures: list[str] = []
    if document.get("schema") != SCHEMA:
        failures.append("schema must be poison.rollout-health/v1")
    metrics = document.get("metrics")
    if not isinstance(metrics, list) or not metrics:
        failures.append("metrics must be a non-empty list")
        metrics = []
    seen: set[str] = set()
    results: list[dict[str, Any]] = []
    for index, metric in enumerate(metrics):
        prefix = f"metrics[{index}]"
        if not isinstance(metric, dict):
            failures.append(f"{prefix} must be an object")
            continue
        metric_id = metric.get("id")
        observed = metric.get("observed")
        threshold = metric.get("threshold")
        halt_if = metric.get("haltIf")
        if not isinstance(metric_id, str) or not metric_id or metric_id in seen:
            failures.append(f"{prefix}.id must be unique and non-empty")
            continue
        seen.add(metric_id)
        if not isinstance(observed, (int, float)) or isinstance(observed, bool) or not math.isfinite(observed):
            failures.append(f"{prefix}.observed must be finite")
            continue
        if not isinstance(threshold, (int, float)) or isinstance(threshold, bool) or not math.isfinite(threshold):
            failures.append(f"{prefix}.threshold must be finite")
            continue
        if halt_if not in {"gte", "lte"}:
            failures.append(f"{prefix}.haltIf must be gte or lte")
            continue
        halted = observed >= threshold if halt_if == "gte" else observed <= threshold
        results.append({"id": metric_id, "observed": observed, "threshold": threshold, "halted": halted})
    halted = any(result["halted"] for result in results)
    return {"schema": "poison.rollout-health-result/v1", "result": "FAIL" if failures else ("HALT" if halted else "PASS"), "halted": halted, "failures": failures, "metrics": results}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", action="store_true", help="evaluate the local candidate artifact")
    parser.add_argument("path", nargs="?", type=Path)
    args = parser.parse_args()
    path = args.path or (DEFAULT_CANDIDATE if args.candidate else None)
    if path is None:
        parser.error("provide a path or --candidate")
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        print(json.dumps({"schema": "poison.rollout-health-result/v1", "result": "FAIL", "failures": [str(error)]}, indent=2, sort_keys=True))
        return 1
    result = evaluate(document)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
