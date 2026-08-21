#!/usr/bin/env python3
"""Validate component licensing and generate deterministic SPDX/CycloneDX SBOMs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys
from typing import Any, Iterable


COMPONENT_SCHEMA = "poison.components/v1"
LICENSE_SCHEMA = "poison.licenses/v1"
CREATED = "2026-08-21T00:00:00Z"
REQUIRED_COMPONENT_FIELDS = (
    "name",
    "path",
    "version",
    "sha256",
    "licenseExpression",
    "licenseIds",
    "sourceUrl",
    "modificationStatus",
)
ALLOWED_MODIFICATION_STATUSES = {
    "unmodified",
    "poison-modified",
    "vendored-unmodified",
    "vendor-metadata-adapted",
}
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}\Z")


class SbomError(ValueError):
    """Raised when component or license evidence is invalid."""


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise SbomError(f"cannot load {path}: {error}") from error
    if not isinstance(value, dict):
        raise SbomError(f"{path} must contain a JSON object")
    return value


def normalized_relative_path(value: Any, field: str, allow_root: bool = False) -> str:
    if not isinstance(value, str) or not value:
        raise SbomError(f"{field} must be a non-empty string")
    if allow_root and value == ".":
        return value
    path = PurePosixPath(value)
    if path.is_absolute() or value != path.as_posix() or ".." in path.parts:
        raise SbomError(f"{field} must be a normalized relative POSIX path")
    return value


def path_mode(path: Path) -> str:
    metadata = path.lstat()
    if stat.S_ISLNK(metadata.st_mode):
        return "120000"
    if stat.S_ISREG(metadata.st_mode):
        return "100755" if metadata.st_mode & 0o111 else "100644"
    raise SbomError(f"unsupported component object: {path}")


def path_digest(path: Path) -> str:
    if path.is_symlink():
        return hashlib.sha256(os.readlink(path).encode("utf-8")).hexdigest()
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def excluded(relative: str, exclude_paths: tuple[str, ...]) -> bool:
    parts = PurePosixPath(relative).parts
    if ".git" in parts or "__pycache__" in parts:
        return True
    return any(relative == item or relative.startswith(f"{item}/") for item in exclude_paths)


def component_digest(root: Path, component: dict[str, Any]) -> str:
    component_path = normalized_relative_path(component["path"], "path", allow_root=True)
    base = root if component_path == "." else root / component_path
    if not base.exists() and not base.is_symlink():
        raise SbomError(f"component path does not exist: {component_path}")
    raw_excludes = component.get("digestExcludes", [])
    if not isinstance(raw_excludes, list):
        raise SbomError(f"digestExcludes must be an array: {component['name']}")
    exclude_paths = tuple(
        normalized_relative_path(item, "digestExcludes[]") for item in raw_excludes
    )

    records: list[str] = []
    if base.is_file() or base.is_symlink():
        records.append(f"{base.name}\0{path_mode(base)}\0{path_digest(base)}\n")
    else:
        for directory, directory_names, file_names in os.walk(base, followlinks=False):
            directory_path = Path(directory)
            relative_directory = directory_path.relative_to(base)
            retained: list[str] = []
            for name in sorted(directory_names):
                child = directory_path / name
                relative = (relative_directory / name).as_posix()
                if excluded(relative, exclude_paths):
                    continue
                if child.is_symlink():
                    records.append(
                        f"{relative}\0{path_mode(child)}\0{path_digest(child)}\n"
                    )
                else:
                    retained.append(name)
            directory_names[:] = retained
            for name in sorted(file_names):
                child = directory_path / name
                relative = (relative_directory / name).as_posix()
                if not excluded(relative, exclude_paths):
                    records.append(
                        f"{relative}\0{path_mode(child)}\0{path_digest(child)}\n"
                    )
    return hashlib.sha256("".join(sorted(records)).encode("utf-8")).hexdigest()


def validate_licenses(root: Path, manifest: dict[str, Any]) -> dict[str, dict[str, Any]]:
    if manifest.get("schema") != LICENSE_SCHEMA:
        raise SbomError(f"license schema must equal {LICENSE_SCHEMA}")
    raw_licenses = manifest.get("licenses")
    if not isinstance(raw_licenses, list) or not raw_licenses:
        raise SbomError("licenses must be a non-empty array")
    licenses: dict[str, dict[str, Any]] = {}
    for index, license_entry in enumerate(raw_licenses):
        if not isinstance(license_entry, dict):
            raise SbomError(f"licenses[{index}] must be an object")
        for field in (
            "id",
            "name",
            "standard",
            "redistributionAllowed",
            "evidencePaths",
        ):
            if field not in license_entry:
                raise SbomError(f"license missing required field: {field}")
        license_id = license_entry["id"]
        if not isinstance(license_id, str) or not license_id:
            raise SbomError("license id must be a non-empty string")
        if license_id in licenses:
            raise SbomError(f"duplicate license: {license_id}")
        if not isinstance(license_entry["standard"], bool):
            raise SbomError(f"license standard must be boolean: {license_id}")
        if not isinstance(license_entry["redistributionAllowed"], bool):
            raise SbomError(
                f"license redistributionAllowed must be boolean: {license_id}"
            )
        if not license_entry["standard"] and not license_id.startswith("LicenseRef-"):
            raise SbomError(f"custom license must use LicenseRef- prefix: {license_id}")
        if not license_entry["standard"] and not isinstance(
            license_entry.get("extractedText"), str
        ):
            raise SbomError(f"custom license requires extractedText: {license_id}")
        evidence = license_entry["evidencePaths"]
        if not isinstance(evidence, list) or not evidence:
            raise SbomError(f"license evidencePaths must be non-empty: {license_id}")
        for evidence_path in evidence:
            normalized = normalized_relative_path(evidence_path, "evidencePaths[]")
            if not (root / normalized).exists():
                raise SbomError(f"license evidence path does not exist: {normalized}")
        licenses[license_id] = license_entry
    return licenses


def validate_components(
    root: Path, manifest: dict[str, Any], licenses: dict[str, dict[str, Any]]
) -> list[dict[str, Any]]:
    if manifest.get("schema") != COMPONENT_SCHEMA:
        raise SbomError(f"component schema must equal {COMPONENT_SCHEMA}")
    raw_components = manifest.get("components")
    if not isinstance(raw_components, list) or not raw_components:
        raise SbomError("components must be a non-empty array")
    components: list[dict[str, Any]] = []
    names: set[str] = set()
    for index, component in enumerate(raw_components):
        if not isinstance(component, dict):
            raise SbomError(f"components[{index}] must be an object")
        for field in REQUIRED_COMPONENT_FIELDS:
            if field not in component:
                raise SbomError(f"missing required field: {field}")
        name = component["name"]
        if not isinstance(name, str) or not name or name in names:
            raise SbomError(f"invalid or duplicate component name: {name!r}")
        names.add(name)
        normalized_relative_path(component["path"], "path", allow_root=True)
        for field in ("version", "licenseExpression", "sourceUrl", "modificationStatus"):
            if not isinstance(component[field], str) or not component[field]:
                raise SbomError(f"component field must be non-empty: {name}.{field}")
        if not component["sourceUrl"].startswith("https://"):
            raise SbomError(f"sourceUrl must use HTTPS: {name}")
        if component["modificationStatus"] not in ALLOWED_MODIFICATION_STATUSES:
            raise SbomError(f"invalid modificationStatus: {name}")
        release_blocked = component.get("releaseBlocked", False)
        if not isinstance(release_blocked, bool):
            raise SbomError(f"releaseBlocked must be boolean: {name}")
        blocker_reason = component.get("releaseBlockerReason")
        if release_blocked and (not isinstance(blocker_reason, str) or not blocker_reason):
            raise SbomError(f"releaseBlocked component requires releaseBlockerReason: {name}")
        if not release_blocked and blocker_reason is not None:
            raise SbomError(f"releaseBlockerReason requires releaseBlocked=true: {name}")
        digest = component["sha256"]
        if not isinstance(digest, str) or SHA256_PATTERN.fullmatch(digest) is None:
            raise SbomError(f"invalid sha256: {name}")
        license_ids = component["licenseIds"]
        if not isinstance(license_ids, list) or not license_ids:
            raise SbomError(f"licenseIds must be non-empty: {name}")
        for license_id in license_ids:
            if license_id not in licenses:
                raise SbomError(f"unknown license: {license_id}")
            if license_id not in component["licenseExpression"]:
                raise SbomError(f"license expression omits {license_id}: {name}")
        if component_digest(root, component) != digest:
            raise SbomError(f"component digest mismatch: {name}")
        components.append(component)
    return sorted(components, key=lambda item: item["name"])


def identifier(name: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9.-]+", "-", name).strip("-")
    return cleaned or "component"


def document_namespace(components: Iterable[dict[str, Any]]) -> str:
    material = "".join(
        f"{component['name']}\0{component['version']}\0{component['sha256']}\n"
        for component in components
    )
    digest = hashlib.sha256(material.encode("utf-8")).hexdigest()
    return f"https://poisoned.invalid/sbom/{digest}"


def redistribution_allowed(
    component: dict[str, Any], licenses: dict[str, dict[str, Any]]
) -> bool:
    return all(licenses[license_id]["redistributionAllowed"] for license_id in component["licenseIds"])


def release_blocked(
    component: dict[str, Any], licenses: dict[str, dict[str, Any]]
) -> bool:
    return bool(component.get("releaseBlocked", False)) or not redistribution_allowed(
        component, licenses
    )


def release_blocker_reason(
    component: dict[str, Any], licenses: dict[str, dict[str, Any]]
) -> str | None:
    if component.get("releaseBlocked", False):
        return component["releaseBlockerReason"]
    reasons = [
        licenses[license_id].get("reviewNote", f"{license_id} forbids redistribution")
        for license_id in component["licenseIds"]
        if not licenses[license_id]["redistributionAllowed"]
    ]
    return "; ".join(reasons) or None


def spdx_document(
    components: list[dict[str, Any]], licenses: dict[str, dict[str, Any]]
) -> dict[str, Any]:
    packages = []
    for index, component in enumerate(components, start=1):
        comment = (
            "redistributionAllowed="
            + str(redistribution_allowed(component, licenses)).lower()
            + "; releaseBlocked="
            + str(release_blocked(component, licenses)).lower()
        )
        blocker_reason = release_blocker_reason(component, licenses)
        if blocker_reason is not None:
            comment += f"; releaseBlockerReason={blocker_reason}"
        packages.append(
            {
                "SPDXID": f"SPDXRef-Package-{index}-{identifier(component['name'])}",
                "name": component["name"],
                "versionInfo": component["version"],
                "downloadLocation": component["sourceUrl"],
                "filesAnalyzed": False,
                "licenseConcluded": component["licenseExpression"],
                "licenseDeclared": component["licenseExpression"],
                "checksums": [
                    {"algorithm": "SHA256", "checksumValue": component["sha256"]}
                ],
                "comment": comment,
                "externalRefs": [
                    {
                        "referenceCategory": "OTHER",
                        "referenceType": "poisonedos:modification-status",
                        "referenceLocator": component["modificationStatus"],
                    }
                ],
            }
        )
    document = {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": "PoisonedOS",
        "documentNamespace": document_namespace(components),
        "creationInfo": {"created": CREATED, "creators": ["Tool: PoisonedOS generate_sbom.py"]},
        "packages": packages,
    }
    extracted = [
        {
            "licenseId": license_id,
            "name": license_entry["name"],
            "extractedText": license_entry["extractedText"],
        }
        for license_id, license_entry in sorted(licenses.items())
        if not license_entry["standard"]
    ]
    if extracted:
        document["hasExtractedLicensingInfos"] = extracted
    return document


def deterministic_uuid(namespace: str) -> str:
    value = hashlib.sha256(namespace.encode("utf-8")).hexdigest()[:32]
    return f"{value[:8]}-{value[8:12]}-{value[12:16]}-{value[16:20]}-{value[20:]}"


def cyclonedx_document(
    components: list[dict[str, Any]], licenses: dict[str, dict[str, Any]]
) -> dict[str, Any]:
    namespace = document_namespace(components)
    return {
        "bomFormat": "CycloneDX",
        "specVersion": "1.6",
        "serialNumber": f"urn:uuid:{deterministic_uuid(namespace)}",
        "version": 1,
        "metadata": {
            "timestamp": CREATED,
            "tools": {"components": [{"type": "application", "name": "generate_sbom.py"}]},
        },
        "components": [
            {
                "type": "library" if component["name"] != "PoisonedOS" else "operating-system",
                "bom-ref": f"component-{index}-{identifier(component['name'])}",
                "name": component["name"],
                "version": component["version"],
                "hashes": [{"alg": "SHA-256", "content": component["sha256"]}],
                "licenses": [{"expression": component["licenseExpression"]}],
                "externalReferences": [
                    {"type": "vcs", "url": component["sourceUrl"]}
                ],
                "properties": [
                    {
                        "name": "poisonedos:modification-status",
                        "value": component["modificationStatus"],
                    },
                    {
                        "name": "poisonedos:redistribution-allowed",
                        "value": str(
                            redistribution_allowed(component, licenses)
                        ).lower(),
                    },
                    {
                        "name": "poisonedos:release-blocked",
                        "value": str(release_blocked(component, licenses)).lower(),
                    },
                    *(
                        [
                            {
                                "name": "poisonedos:release-blocker-reason",
                                "value": release_blocker_reason(component, licenses),
                            }
                        ]
                        if release_blocker_reason(component, licenses) is not None
                        else []
                    ),
                    {"name": "poisonedos:path", "value": component["path"]},
                ],
            }
            for index, component in enumerate(components, start=1)
        ],
    }


def canonical_json(value: dict[str, Any]) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def generate(
    root: Path, components_path: Path, licenses_path: Path
) -> tuple[bytes, bytes, int, int]:
    licenses = validate_licenses(root, load_json(licenses_path))
    components = validate_components(root, load_json(components_path), licenses)
    blocker_count = sum(
        release_blocked(component, licenses) for component in components
    )
    return canonical_json(spdx_document(components, licenses)), canonical_json(
        cyclonedx_document(components, licenses)
    ), len(components), blocker_count


def parse_arguments() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--components", type=Path, default=root / "provenance" / "components.json")
    parser.add_argument("--licenses", type=Path, default=root / "provenance" / "licenses.json")
    parser.add_argument("--output-dir", type=Path, default=root / "artifacts" / "sbom")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--write", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        first_spdx, first_cyclonedx, component_count, blocker_count = generate(
            arguments.root.resolve(),
            arguments.components.resolve(),
            arguments.licenses.resolve(),
        )
        second_spdx, second_cyclonedx, _, second_blocker_count = generate(
            arguments.root.resolve(),
            arguments.components.resolve(),
            arguments.licenses.resolve(),
        )
    except SbomError as error:
        print(error, file=sys.stderr)
        return 1
    if (first_spdx, first_cyclonedx, blocker_count) != (
        second_spdx,
        second_cyclonedx,
        second_blocker_count,
    ):
        print("SBOM generation is not deterministic", file=sys.stderr)
        return 1
    if arguments.write:
        arguments.output_dir.mkdir(parents=True, exist_ok=True)
        (arguments.output_dir / "poisonedos.spdx.json").write_bytes(first_spdx)
        (arguments.output_dir / "poisonedos.cdx.json").write_bytes(first_cyclonedx)
        print(
            f"wrote {component_count} components to {arguments.output_dir} "
            f"({blocker_count} release blockers)"
        )
    else:
        print(
            f"SBOM verification passed: {component_count} components, "
            f"0 unknown licenses, {blocker_count} release blockers"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
