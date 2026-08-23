#!/usr/bin/env python3
"""Validate and bundle JavaScript for the Flipper MJS runtime.

Node and npm are build-time tools.  The device receives one esbuild bundle;
this command makes that boundary explicit and rejects Node-only dependencies.
"""

from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

SCHEMA = "poison.javascript.lock/v1"
RUNTIME = "poison-mjs-1"
MAX_SOURCE_FILES = 256
MAX_BUNDLE_BYTES = 4 * 1024 * 1024
MAX_DEPENDENCY_FILES = 256
MAX_DEPENDENCY_FILE_BYTES = 256 * 1024
MAX_DEPENDENCY_DEPTH = 16
NODE_BUILTINS = {
    "assert", "buffer", "child_process", "cluster", "crypto", "dgram", "dns",
    "events", "fs", "http", "https", "module", "net", "os", "path",
    "perf_hooks", "process", "stream", "string_decoder", "timers", "tls",
    "url", "util", "worker_threads", "zlib",
}
SUPPORTED_BUILTINS = {"assert", "buffer", "crypto", "events", "fs", "http", "https", "net", "os", "path", "process", "querystring", "stream", "string_decoder", "timers", "tls", "url", "util"}
IMPORT_RE = re.compile(r"(?:import(?:[^'\"]*?from\s*)?|require\s*\()(['\"])([^'\"]+)\1")
DEPENDENCY_NAME_RE = re.compile(r"(?:@[a-z0-9][a-z0-9._-]*/)?[a-z0-9][a-z0-9._-]*")
VERSION_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,127}")
LICENSE_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9.()+\- ]{0,127}")
DIGEST_RE = re.compile(r"[0-9a-f]{64}")
PATH_SEGMENT_RE = re.compile(r"[A-Za-z0-9@_+-][A-Za-z0-9@._+-]*")
DEPENDENCY_FIELDS = {
    "name", "version", "main", "integrity", "source", "license", "runtime", "dependencies", "files",
}
FILE_FIELDS = {"path", "sha256", "bytes"}


class VendorError(ValueError):
    pass


def _safe_dependency_path(value: object) -> bool:
    if not isinstance(value, str) or not value or len(value) > 160 or "\\" in value:
        return False
    path = Path(value)
    return not path.is_absolute() and all(PATH_SEGMENT_RE.fullmatch(part) for part in path.parts) and path.suffix in {
        ".js", ".mjs", ".cjs", ".ts", ".json",
    }


def _integrity_digest(value: object) -> bytes:
    if not isinstance(value, str) or not value.startswith("sha256-"):
        raise VendorError("dependency integrity must be a SHA-256 digest")
    try:
        digest = base64.b64decode(value[7:], validate=True)
    except (binascii.Error, ValueError) as error:
        raise VendorError("dependency integrity must be valid base64") from error
    if len(digest) != hashlib.sha256().digest_size:
        raise VendorError("dependency integrity must contain exactly one SHA-256 digest")
    return digest


def _validate_dependency_graph(dependencies: list[dict]) -> None:
    by_name = {dependency["name"]: dependency for dependency in dependencies}
    for dependency in dependencies:
        for required in dependency["dependencies"]:
            if required not in by_name:
                raise VendorError(f"dependency {dependency['name']} references unknown dependency {required}")

    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(name: str, depth: int) -> None:
        if depth > MAX_DEPENDENCY_DEPTH:
            raise VendorError(f"dependency graph exceeds depth {MAX_DEPENDENCY_DEPTH}")
        if name in visiting:
            raise VendorError(f"dependency graph contains a cycle at {name}")
        if name in visited:
            return
        visiting.add(name)
        for required in by_name[name]["dependencies"]:
            visit(required, depth + 1)
        visiting.remove(name)
        visited.add(name)

    for name in sorted(by_name):
        visit(name, 1)


def load_lock(path: Path) -> dict:
    try:
        lock = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise VendorError(f"invalid lock file: {exc}") from exc
    if not isinstance(lock, dict) or lock.get("schema") != SCHEMA:
        raise VendorError(f"lock schema must be {SCHEMA}")
    if not {"schema", "runtime", "entrypoint", "dependencies"}.issubset(lock) or not set(lock).issubset(
        {"schema", "runtime", "entrypoint", "dependencies", "allow"}
    ):
        raise VendorError("lock has missing or unknown fields")
    if lock.get("runtime") != RUNTIME:
        raise VendorError(f"lock runtime must be {RUNTIME}")
    entry = lock.get("entrypoint")
    if not isinstance(entry, str) or Path(entry).is_absolute() or ".." in Path(entry).parts:
        raise VendorError("entrypoint must be a relative project path")
    if Path(entry).suffix not in {".js", ".mjs", ".cjs", ".ts"}:
        raise VendorError("entrypoint must be JavaScript or TypeScript")
    dependencies = lock.get("dependencies")
    if not isinstance(dependencies, list) or len(dependencies) > 256:
        raise VendorError("dependencies must be a bounded array")
    names = set()
    total_files = 0
    for dep in dependencies:
        if not isinstance(dep, dict) or set(dep) != DEPENDENCY_FIELDS:
            raise VendorError("each dependency needs identity, license, runtime, graph, and file metadata")
        if not isinstance(dep["name"], str) or not DEPENDENCY_NAME_RE.fullmatch(dep["name"]) or dep["name"] in names:
            raise VendorError("dependency names must be unique safe package names")
        if not isinstance(dep["version"], str) or not VERSION_RE.fullmatch(dep["version"]):
            raise VendorError(f"invalid dependency version for {dep['name']}")
        _integrity_digest(dep["integrity"])
        if dep["source"] not in {"registry", "workspace", "bundled"}:
            raise VendorError(f"unsupported source for {dep['name']}")
        if not isinstance(dep["license"], str) or not LICENSE_RE.fullmatch(dep["license"]):
            raise VendorError(f"invalid dependency license for {dep['name']}")
        if dep["runtime"] != RUNTIME:
            raise VendorError(f"dependency {dep['name']} does not support runtime {RUNTIME}")
        if not isinstance(dep["dependencies"], list) or len(dep["dependencies"]) > 256 or any(
            not isinstance(required, str) or not DEPENDENCY_NAME_RE.fullmatch(required)
            for required in dep["dependencies"]
        ) or len(set(dep["dependencies"])) != len(dep["dependencies"]):
            raise VendorError(f"invalid dependency graph edges for {dep['name']}")
        files = dep["files"]
        if not isinstance(files, list) or not files or len(files) > MAX_DEPENDENCY_FILES:
            raise VendorError(f"dependency {dep['name']} needs a bounded file inventory")
        paths = set()
        for file in files:
            if not isinstance(file, dict) or set(file) != FILE_FIELDS or not _safe_dependency_path(file.get("path")):
                raise VendorError(f"dependency {dep['name']} has invalid file metadata")
            if file["path"] in paths or not isinstance(file.get("sha256"), str) or not DIGEST_RE.fullmatch(file["sha256"]):
                raise VendorError(f"dependency {dep['name']} has duplicate paths or invalid file digests")
            if not isinstance(file.get("bytes"), int) or isinstance(file["bytes"], bool) or not 0 <= file["bytes"] <= MAX_DEPENDENCY_FILE_BYTES:
                raise VendorError(f"dependency {dep['name']} has an invalid file size")
            paths.add(file["path"])
        if not _safe_dependency_path(dep["main"]) or Path(dep["main"]).suffix not in {".js", ".mjs", ".cjs"} or dep["main"] not in paths:
            raise VendorError(f"dependency {dep['name']} main must name an executable file in its inventory")
        total_files += len(files)
        names.add(dep["name"])
    if total_files > MAX_DEPENDENCY_FILES:
        raise VendorError(f"dependency file count exceeds {MAX_DEPENDENCY_FILES}")
    _validate_dependency_graph(dependencies)
    allow = lock.get("allow", [])
    if not isinstance(allow, list) or any(item != "@flipperdevices/fz-sdk" for item in allow):
        raise VendorError("only the native Flipper SDK may be allowed as an external module")
    return lock


def verify_vendored_dependencies(project: Path, lock: dict) -> list[str]:
    project_root = project.resolve()
    verified_paths: list[str] = []
    total_bytes = 0
    for dependency in sorted(lock["dependencies"], key=lambda item: item["name"]):
        package_root = project_root / "vendor" / dependency["name"] / dependency["version"]
        if package_root.absolute() != package_root.resolve() or not package_root.is_dir():
            raise VendorError(f"vendored dependency root is missing or redirected: {dependency['name']}")
        expected_paths = {file["path"] for file in dependency["files"]}
        actual_paths = {
            str(path.relative_to(package_root))
            for path in package_root.rglob("*")
            if path.is_file()
        }
        if actual_paths != expected_paths:
            raise VendorError(f"vendored dependency inventory mismatch: {dependency['name']}")
        package_hash = hashlib.sha256()
        for descriptor in sorted(dependency["files"], key=lambda item: item["path"]):
            candidate = package_root / descriptor["path"]
            if candidate.absolute() != candidate.resolve() or project_root not in candidate.resolve().parents:
                raise VendorError(f"vendored dependency path is redirected: {dependency['name']}/{descriptor['path']}")
            data = candidate.read_bytes()
            if len(data) != descriptor["bytes"] or hashlib.sha256(data).hexdigest() != descriptor["sha256"]:
                raise VendorError(f"vendored dependency file digest mismatch: {dependency['name']}/{descriptor['path']}")
            package_hash.update(descriptor["path"].encode("utf-8"))
            package_hash.update(b"\0")
            package_hash.update(data)
            total_bytes += len(data)
            if total_bytes > MAX_BUNDLE_BYTES:
                raise VendorError(f"vendored dependencies exceed {MAX_BUNDLE_BYTES} bytes")
            verified_paths.append(str(candidate.relative_to(project_root)))
        if package_hash.digest() != _integrity_digest(dependency["integrity"]):
            raise VendorError(f"vendored dependency package digest mismatch: {dependency['name']}")
    return sorted(verified_paths)


def scan_sources(project: Path, lock: dict) -> list[str]:
    verify_vendored_dependencies(project, lock)
    entry = project / lock["entrypoint"]
    if not entry.is_file():
        raise VendorError(f"entrypoint does not exist: {lock['entrypoint']}")
    files = [p for p in project.rglob("*") if p.is_file() and p.suffix in {".js", ".mjs", ".cjs", ".ts", ".tsx"}
             and "node_modules" not in p.parts and "dist" not in p.parts]
    if len(files) > MAX_SOURCE_FILES:
        raise VendorError(f"source file count exceeds {MAX_SOURCE_FILES}")
    violations = []
    for path in sorted(files):
        text = path.read_text(encoding="utf-8")
        for _, specifier in IMPORT_RE.findall(text):
            builtin = specifier.removeprefix("node:")
            if builtin in NODE_BUILTINS and builtin not in SUPPORTED_BUILTINS:
                violations.append(f"{path.relative_to(project)} imports Node builtin {specifier}")
            elif specifier.startswith("@flipperdevices/fz-sdk/") and "@flipperdevices/fz-sdk" not in lock.get("allow", []):
                violations.append(f"{path.relative_to(project)} uses unapproved SDK module {specifier}")
    if violations:
        raise VendorError("unsupported device dependencies:\n" + "\n".join(violations))
    return [str(p.relative_to(project)) for p in sorted(files)]


def build(project: Path, lock_path: Path, output: Path, provenance: Path | None) -> dict:
    lock = load_lock(lock_path)
    sources = scan_sources(project, lock)
    config = project / "fz-sdk.config.json5"
    if not config.is_file():
        raise VendorError("project must provide fz-sdk.config.json5 for the official SDK bundler")
    sdk = Path(__file__).resolve().parents[2] / "applications/system/js_app/packages/fz-sdk/sdk.js"
    result = subprocess.run(["node", str(sdk), "build"], cwd=project, text=True, capture_output=True)
    if result.returncode:
        raise VendorError(result.stderr.strip() or result.stdout.strip() or "SDK bundler failed")
    # The official config owns the output path; copy is intentionally avoided so
    # its output remains the artifact users test and upload.
    output = output.resolve()
    config_text = config.read_text(encoding="utf-8")
    match = re.search(r"output\s*:\s*[\"']([^\"']+)[\"']", config_text)
    configured = project / match.group(1) if match else project / "dist" / (project.name + ".js")
    if not configured.is_file():
        candidates = sorted((project / "dist").glob("*.js")) if (project / "dist").is_dir() else []
        if len(candidates) != 1:
            raise VendorError("SDK build produced no unambiguous dist/*.js artifact")
        configured = candidates[0]
    data = configured.read_bytes()
    if len(data) > MAX_BUNDLE_BYTES:
        raise VendorError(f"bundle exceeds {MAX_BUNDLE_BYTES} bytes")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    record = {"schema": "poison.javascript.bundle/v1", "runtime": RUNTIME,
              "entrypoint": lock["entrypoint"], "sources": sources,
              "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}
    if provenance:
        provenance.parent.mkdir(parents=True, exist_ok=True)
        provenance.write_text(json.dumps(record, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return record


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("lock", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--provenance", type=Path)
    args = parser.parse_args(argv)
    try:
        lock = load_lock(args.lock)
        sources = scan_sources(args.project, lock)
        if args.output:
            record = build(args.project, args.lock, args.output, args.provenance)
        else:
            record = {"schema": SCHEMA, "runtime": RUNTIME, "entrypoint": lock["entrypoint"], "sources": sources}
        print(json.dumps(record, sort_keys=True))
        return 0
    except VendorError as exc:
        print(f"vendor_package: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
