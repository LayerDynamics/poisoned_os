from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
COMPARER = REPOSITORY_ROOT / "tools" / "compare_upstream.py"


def run_git(repository: Path, *arguments: str) -> str:
    return subprocess.check_output(
        ["git", "-C", os.fspath(repository), *arguments], text=True
    ).strip()


class UpstreamFixture:
    def __init__(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name) / "product"
        self.source = self.root / "do_not_include" / "upstream"
        self.source.mkdir(parents=True)
        run_git(self.source, "init", "-b", "dev")
        run_git(self.source, "config", "user.name", "PoisonedOS Test")
        run_git(self.source, "config", "user.email", "test@poisoned.invalid")

        self.write_source("identical.txt", b"same\n")
        self.write_source("modified.txt", b"official\n")
        self.write_source("omitted.txt", b"official only\n")
        self.write_source("mode.sh", b"#!/bin/sh\n")
        (self.source / "mode.sh").chmod(0o755)
        run_git(self.source, "add", "identical.txt", "modified.txt", "omitted.txt", "mode.sh")
        run_git(self.source, "commit", "-m", "fixture files")

        self.dependency_commit = "1" * 40
        run_git(
            self.source,
            "update-index",
            "--add",
            "--cacheinfo",
            f"160000,{self.dependency_commit},deps/lib",
        )
        run_git(self.source, "commit", "-m", "fixture dependency")

        self.write_product("identical.txt", b"same\n")
        self.write_product("modified.txt", b"poisoned\n")
        self.write_product("mode.sh", b"#!/bin/sh\n")
        self.write_product("added.txt", b"product only\n")
        self.write_product("deps/lib/source.c", b"vendored dependency\n")

    def write_source(self, path: str, data: bytes) -> None:
        destination = self.source / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)

    def write_product(self, path: str, data: bytes) -> None:
        destination = self.root / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)

    def close(self) -> None:
        self.temporary.cleanup()


class CompareUpstreamTests(unittest.TestCase):
    def setUp(self) -> None:
        self.fixture = UpstreamFixture()

    def tearDown(self) -> None:
        self.fixture.close()

    def compare(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                os.fspath(COMPARER),
                "--root",
                os.fspath(self.fixture.root),
                "--baseline",
                os.fspath(self.fixture.source),
                "--stdout",
            ],
            capture_output=True,
            check=False,
            text=True,
        )

    def load_report(self) -> dict[str, object]:
        import json

        result = self.compare()
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def entries(self) -> dict[str, dict[str, object]]:
        report = self.load_report()
        return {entry["path"]: entry for entry in report["paths"]}

    def test_classifies_identical_upstream_file(self) -> None:
        self.assertEqual(self.entries()["identical.txt"]["classification"], "identical")

    def test_classifies_changed_upstream_content(self) -> None:
        entry = self.entries()["modified.txt"]
        self.assertEqual(entry["classification"], "poison-modified")
        self.assertEqual(entry["differences"], ["content"])

    def test_classifies_upstream_only_path(self) -> None:
        self.assertEqual(self.entries()["omitted.txt"]["classification"], "upstream-omitted")

    def test_classifies_poisoned_only_path(self) -> None:
        self.assertEqual(self.entries()["added.txt"]["classification"], "poison-added")

    def test_classifies_changed_mode(self) -> None:
        entry = self.entries()["mode.sh"]
        self.assertEqual(entry["classification"], "poison-modified")
        self.assertEqual(entry["differences"], ["mode"])

    def test_classifies_gitlink_and_vendored_dependency_contents(self) -> None:
        entries = self.entries()
        self.assertEqual(entries["deps/lib"]["classification"], "dependency")
        self.assertEqual(entries["deps/lib"]["upstreamCommit"], self.fixture.dependency_commit)
        self.assertEqual(entries["deps/lib/source.c"]["classification"], "dependency")
        self.assertEqual(entries["deps/lib/source.c"]["dependencyPath"], "deps/lib")

    def test_excludes_comparison_repository_from_product_provenance(self) -> None:
        paths = self.entries()
        self.assertFalse(any(path.startswith("do_not_include/") for path in paths))
        self.assertFalse(any("/.git/" in path for path in paths))

    def test_excludes_the_recursive_baseline_manifest(self) -> None:
        self.fixture.write_product("provenance/baseline.lock.json", b"{}\n")
        self.assertNotIn("provenance/baseline.lock.json", self.entries())

    def test_excludes_nested_git_pointer_files(self) -> None:
        self.fixture.write_product("deps/lib/.git", b"gitdir: /comparison/repository\n")
        self.assertNotIn("deps/lib/.git", self.entries())


if __name__ == "__main__":
    unittest.main()
