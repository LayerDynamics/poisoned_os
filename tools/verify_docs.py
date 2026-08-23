#!/usr/bin/env python3
"""Verify structural and repository-reference invariants in Markdown documentation."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys


FENCE_PATTERN = re.compile(r"^\s*(`{3,}|~{3,})(.*)$")
HEADING_PATTERN = re.compile(r"^(#{1,6})\s+(.+?)\s*$")
INLINE_CODE_PATTERN = re.compile(r"`([^`\n]+)`")
PRECISE_PATH_PATTERN = re.compile(
    r"(?P<path>[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)+):(?P<line>\d+)(?:-(?P<end>\d+))?\Z"
)
REQUIREMENT_PATTERN = re.compile(r"^\|\s*([A-Z][A-Z0-9]*-\d+)\s*\|")


@dataclass(frozen=True)
class Heading:
    line: int
    level: int
    title: str


def validate_document(root: Path, path: Path) -> tuple[list[str], list[tuple[str, int]]]:
    relative = path.relative_to(root).as_posix()
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    errors: list[str] = []
    requirements: list[tuple[str, int]] = []
    headings: list[Heading] = []
    fence_marker: str | None = None

    for line_number, line in enumerate(lines, start=1):
        fence = FENCE_PATTERN.match(line)
        if fence_marker is not None:
            if (
                fence is not None
                and fence.group(1)[0] == fence_marker[0]
                and len(fence.group(1)) >= len(fence_marker)
                and not fence.group(2).strip()
            ):
                fence_marker = None
            continue

        if fence is not None:
            fence_marker = fence.group(1)
            if not fence.group(2).strip():
                errors.append(f"{relative}:{line_number}: untagged code fence")
            continue

        heading = HEADING_PATTERN.match(line)
        if heading is not None:
            headings.append(
                Heading(line_number, len(heading.group(1)), heading.group(2).strip())
            )

        for inline in INLINE_CODE_PATTERN.finditer(line):
            citation = PRECISE_PATH_PATTERN.fullmatch(inline.group(1))
            if citation is None:
                continue
            cited_path = citation.group("path")
            target = root / cited_path
            if not target.is_file():
                errors.append(
                    f"{relative}:{line_number}: repository path does not exist: {cited_path}"
                )
                continue
            start = int(citation.group("line"))
            end = int(citation.group("end") or start)
            target_lines = len(target.read_bytes().splitlines())
            if start < 1 or end < start or end > target_lines:
                errors.append(
                    f"{relative}:{line_number}: repository line citation is out of range: "
                    f"{inline.group(1)}"
                )

        if relative.startswith("docs/specs/"):
            requirement = REQUIREMENT_PATTERN.match(line)
            if requirement is not None:
                requirements.append((requirement.group(1), line_number))

    if fence_marker is not None:
        errors.append(f"{relative}:{len(lines)}: unclosed code fence")

    for index, heading in enumerate(headings):
        end_line = len(lines) + 1
        for following in headings[index + 1 :]:
            if following.level <= heading.level:
                end_line = following.line
                break
        body = lines[heading.line : end_line - 1]
        if not any(
            candidate.strip()
            and HEADING_PATTERN.match(candidate) is None
            and not candidate.lstrip().startswith("<!--")
            for candidate in body
        ):
            errors.append(
                f"{relative}:{heading.line}: empty section: {heading.title}"
            )

    return errors, requirements


def verify(root: Path) -> tuple[int, list[str]]:
    docs = root / "docs"
    if not docs.is_dir():
        return 0, ["docs directory does not exist"]
    paths = sorted(docs.rglob("*.md"))
    errors: list[str] = []
    definitions: dict[str, tuple[str, int]] = {}
    for path in paths:
        document_errors, requirements = validate_document(root, path)
        errors.extend(document_errors)
        relative = path.relative_to(root).as_posix()
        for requirement, line_number in requirements:
            if previous := definitions.get(requirement):
                errors.append(
                    f"{relative}:{line_number}: duplicate requirement ID: {requirement}; "
                    f"first defined at {previous[0]}:{previous[1]}"
                )
            else:
                definitions[requirement] = (relative, line_number)
    return len(paths), errors


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        count, errors = verify(arguments.root.resolve())
    except (OSError, UnicodeError) as error:
        print(f"cannot verify documentation: {error}", file=sys.stderr)
        return 1
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(f"documentation verification passed: {count} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
