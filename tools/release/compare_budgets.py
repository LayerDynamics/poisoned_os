#!/usr/bin/env python3
"""Compare measured performance distributions with declared release budgets."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any

SCHEMA = "poison.performance/v1"


class BudgetError(ValueError):
    pass


def _percentile(values: list[float], percentile: float) -> float:
    ordered = sorted(values)
    position = (len(ordered) - 1) * percentile
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def compare(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise BudgetError(f"cannot load performance evidence: {error}") from error
    if not isinstance(document, dict) or document.get("schema") != SCHEMA or not isinstance(document.get("measurements"), list):
        raise BudgetError(f"performance evidence must use {SCHEMA}")
    seen: set[str] = set()
    failures: list[str] = []
    results: list[dict[str, Any]] = []
    for index, item in enumerate(document["measurements"]):
        if not isinstance(item, dict):
            raise BudgetError(f"measurement {index} must be an object")
        identifier = item.get("id")
        values = item.get("values")
        budget = item.get("budget")
        if not isinstance(identifier, str) or not identifier or identifier in seen:
            raise BudgetError(f"measurement {index} has a duplicate or invalid id")
        seen.add(identifier)
        if not isinstance(values, list) or not values or any(isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0 for value in values):
            raise BudgetError(f"{identifier} values must be finite non-negative numbers")
        if not isinstance(budget, dict) or not isinstance(budget.get("samples"), int) or budget["samples"] < 1:
            raise BudgetError(f"{identifier} must declare a positive sample budget")
        if len(values) < budget["samples"]:
            failures.append(f"{identifier}: insufficient samples ({len(values)} < {budget['samples']})")
        p95 = _percentile([float(value) for value in values], 0.95)
        observed: dict[str, float] = {"p95": p95, "max": float(max(values)), "min": float(min(values))}
        checks: dict[str, bool] = {}
        if "p95" in budget:
            if not isinstance(budget["p95"], (int, float)) or isinstance(budget["p95"], bool) or budget["p95"] < 0: raise BudgetError(f"{identifier} has invalid p95 budget")
            checks["p95"] = p95 <= float(budget["p95"])
        if "max" in budget:
            if not isinstance(budget["max"], (int, float)) or isinstance(budget["max"], bool) or budget["max"] < 0: raise BudgetError(f"{identifier} has invalid max budget")
            checks["max"] = observed["max"] <= float(budget["max"])
        if "min" in budget:
            if not isinstance(budget["min"], (int, float)) or isinstance(budget["min"], bool) or budget["min"] < 0: raise BudgetError(f"{identifier} has invalid min budget")
            checks["min"] = observed["min"] >= float(budget["min"])
        for check, passed in checks.items():
            if not passed: failures.append(f"{identifier}: {check} budget failed")
        results.append({"id": identifier, "samples": len(values), "observed": observed, "checks": checks})
    return {"schema": "poison.performance-comparison/v1", "result": "PASS" if not failures else "FAIL", "failures": failures, "measurements": results}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("evidence", type=Path)
    args = parser.parse_args(argv)
    try:
        result = compare(args.evidence)
    except BudgetError as error:
        print(json.dumps({"schema": "poison.performance-comparison/v1", "result": "FAIL", "failures": [str(error)]}, indent=2, sort_keys=True))
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
