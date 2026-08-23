#!/usr/bin/env python3
"""Validate distribution profiles and local-only dependency boundaries."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
from typing import Any


SCHEMA = "poison.features/v1"
PROFILES = ("local-only", "self-hosted", "hosted")
FEATURE_PROFILE_FIELDS = {
    "local-only": "local",
    "self-hosted": "selfHosted",
    "hosted": "hosted",
}
STATES = {"enabled", "disabled", "configurable"}
ID_PATTERN = re.compile(r"^[a-z][a-z0-9-]*$")


class FeatureMatrixError(ValueError):
    """Raised when a feature matrix violates a distribution invariant."""


def _load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise FeatureMatrixError(f"cannot read feature matrix: {error}") from error
    if not isinstance(value, dict):
        raise FeatureMatrixError("feature matrix root must be an object")
    return value


def _require_string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise FeatureMatrixError(f"{field} must be a non-empty string")
    return value


def _require_bool(value: Any, field: str) -> bool:
    if not isinstance(value, bool):
        raise FeatureMatrixError(f"{field} must be boolean")
    return value


def validate_matrix(matrix: dict[str, Any]) -> dict[str, Any]:
    if matrix.get("schema") != SCHEMA:
        raise FeatureMatrixError(f"schema must be {SCHEMA}")
    if matrix.get("product") != "PoisonedOS":
        raise FeatureMatrixError("product must be PoisonedOS")
    if matrix.get("defaultProfile") != "local-only":
        raise FeatureMatrixError("defaultProfile must be local-only")

    profiles = matrix.get("profiles")
    if not isinstance(profiles, dict) or set(profiles) != set(PROFILES):
        raise FeatureMatrixError("profiles must contain exactly local-only, self-hosted, and hosted")
    for profile_name in PROFILES:
        profile = profiles[profile_name]
        if not isinstance(profile, dict):
            raise FeatureMatrixError(f"profile {profile_name} must be an object")
        _require_bool(profile.get("accountRequired"), f"profiles.{profile_name}.accountRequired")
        _require_bool(profile.get("externalRequests"), f"profiles.{profile_name}.externalRequests")
        _require_string(profile.get("endpointPolicy"), f"profiles.{profile_name}.endpointPolicy")
    if profiles["local-only"]["accountRequired"] or profiles["local-only"]["externalRequests"]:
        raise FeatureMatrixError("local-only profile cannot require an account or external requests")

    features = matrix.get("features")
    if not isinstance(features, list) or not features:
        raise FeatureMatrixError("features must be a non-empty array")
    seen: set[str] = set()
    for index, feature in enumerate(features):
        prefix = f"features[{index}]"
        if not isinstance(feature, dict):
            raise FeatureMatrixError(f"{prefix} must be an object")
        identifier = _require_string(feature.get("id"), f"{prefix}.id")
        if not ID_PATTERN.fullmatch(identifier):
            raise FeatureMatrixError(f"{prefix}.id is not a kebab-case identifier")
        if identifier in seen:
            raise FeatureMatrixError(f"duplicate feature id: {identifier}")
        seen.add(identifier)
        _require_string(feature.get("owner"), f"{prefix}.owner")
        _require_string(feature.get("dataBoundary"), f"{prefix}.dataBoundary")
        hosted_dependency = feature.get("hostedDependency")
        if hosted_dependency not in {"none", "optional", "required"}:
            raise FeatureMatrixError(f"{prefix}.hostedDependency is invalid")
        for profile, field in FEATURE_PROFILE_FIELDS.items():
            if feature.get(field) not in STATES:
                raise FeatureMatrixError(f"{prefix}.{field} is invalid")
        account_required = _require_bool(feature.get("accountRequired"), f"{prefix}.accountRequired")
        local_state = feature[FEATURE_PROFILE_FIELDS["local-only"]]
        if local_state == "enabled" and account_required:
            raise FeatureMatrixError(f"local feature {identifier} cannot require an account")
        if local_state == "enabled" and hosted_dependency == "required":
            raise FeatureMatrixError(f"local feature {identifier} cannot require a hosted dependency")
        if local_state == "enabled" and feature["hostedDependency"] != "none":
            raise FeatureMatrixError(f"enabled local feature {identifier} must have no hosted dependency")

    for field in ("localOnlyScanPaths", "hostedImportPatterns", "externalRequestPatterns"):
        value = matrix.get(field)
        if not isinstance(value, list) or any(not isinstance(item, str) or not item for item in value):
            raise FeatureMatrixError(f"{field} must be a non-empty-string array")
    return matrix


def scan_local_only_sources(root: Path, matrix: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    patterns = tuple(matrix["hostedImportPatterns"] + matrix["externalRequestPatterns"])
    for relative in matrix["localOnlyScanPaths"]:
        path = root / relative
        if not path.exists():
            continue
        files = [path] if path.is_file() else sorted(item for item in path.rglob("*") if item.is_file())
        for source in files:
            if source.name in {"package-lock.json", "pnpm-lock.yaml", "Cargo.lock"}:
                continue
            try:
                text = source.read_text(encoding="utf-8")
            except (OSError, UnicodeError):
                continue
            for line_number, line in enumerate(text.splitlines(), start=1):
                for pattern in patterns:
                    if pattern in line:
                        errors.append(f"{source.relative_to(root)}:{line_number}: local-only forbidden pattern {pattern!r}")
    return errors


def verify(root: Path, config_path: Path) -> list[str]:
    matrix = validate_matrix(_load_json(config_path))
    return scan_local_only_sources(root, matrix)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=Path("config/features/local-only.json"))
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    arguments = parser.parse_args()
    try:
        errors = verify(arguments.root.resolve(), arguments.config.resolve())
    except (FeatureMatrixError, OSError) as error:
        print(f"feature matrix verification failed: {error}", file=sys.stderr)
        return 1
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("feature matrix verification passed: local-only boundary is explicit")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
