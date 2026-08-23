from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
VERIFIER = REPOSITORY_ROOT / "tools" / "verify_docs.py"


class VerifyDocsFixture:
    def __init__(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "docs" / "specs").mkdir(parents=True)
        (self.root / "src").mkdir()
        (self.root / "src" / "source.c").write_text("int value;\n", encoding="utf-8")
        self.write(
            "docs/guide.md",
            """# Guide

The implementation is in `src/source.c:1`.

```text
verified example
```
""",
        )
        self.write(
            "docs/specs/spec.md",
            """# Specification

## Requirements

| ID | Priority | Requirement |
|---|---|---|
| FR-1 | MUST | Perform the operation. |
""",
        )

    def write(self, relative: str, contents: str) -> None:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding="utf-8")

    def run(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(VERIFIER), "--root", str(self.root)],
            capture_output=True,
            check=False,
            text=True,
        )

    def close(self) -> None:
        self.temporary.cleanup()


class VerifyDocsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.fixture = VerifyDocsFixture()

    def tearDown(self) -> None:
        self.fixture.close()

    def test_accepts_tagged_complete_documentation(self) -> None:
        result = self.fixture.run()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "documentation verification passed: 2 files\n")

    def test_rejects_untagged_code_fence(self) -> None:
        self.fixture.write("docs/guide.md", "# Guide\n\n```\nvalue\n```\n")
        result = self.fixture.run()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("untagged code fence", result.stderr)

    def test_rejects_missing_precise_repository_path(self) -> None:
        self.fixture.write(
            "docs/guide.md", "# Guide\n\nSee `src/missing.c:12-14`.\n"
        )
        result = self.fixture.run()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("repository path does not exist: src/missing.c", result.stderr)

    def test_rejects_duplicate_requirement_definition(self) -> None:
        self.fixture.write(
            "docs/specs/second.md",
            """# Second Specification

| ID | Priority | Requirement |
|---|---|---|
| FR-1 | MUST | Duplicate the operation. |
""",
        )
        result = self.fixture.run()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("duplicate requirement ID: FR-1", result.stderr)

    def test_rejects_empty_section(self) -> None:
        self.fixture.write(
            "docs/guide.md",
            """# Guide

## Empty

## Complete

This section has content.
""",
        )
        result = self.fixture.run()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("empty section: Empty", result.stderr)


if __name__ == "__main__":
    unittest.main()
