from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
VERIFIER = REPOSITORY_ROOT / "tools" / "verify_baseline.py"
BASELINE_LOCK = REPOSITORY_ROOT / "provenance" / "baseline.lock.json"
EXPECTED_COMMIT = "a55e39395ff31bd5fdf3929c70720a7fb76e5968"
EXPECTED_DEPENDENCIES = {
    "assets/protobuf": "1c84fa48919cbb71d1cc65236fc0ee36740e24c6",
    "documentation/doxygen/doxygen-awesome-css": "df88fe4fdd97714fadfd3ef17de0b4401f804052",
    "lib/FreeRTOS-Kernel": "def7d2df2b0506d3d249334974f51e427c17a41c",
    "lib/heatshrink": "7398ccc91652a33483245200cfa1a83b073bc206",
    "lib/libusb_stm32": "6ca2857519f996244f7b324dd227fdf0a075fffb",
    "lib/mbedtls": "107ea89daaefb9867ea9121002fbbdf926780e98",
    "lib/microtar": "1e921369b2c92bb219fcef84a37d4d2347794c0f",
    "lib/mlib": "62c8ac3e5d4a7a4f8757328e7a80286fde2686b6",
    "lib/nanopb": "6cfe48d6f1593f8fa5c0f90437f5e6522587745e",
    "lib/stm32wb_cmsis": "d1b860584dfe24d40d455ae624ed14600dfa93c9",
    "lib/stm32wb_copro": "133182d5583e998bb263cd947105be4df9c29cb3",
    "lib/stm32wb_hal": "cfd0dd258cb031c95b2b2d6d04c19f9f625fe3e8",
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def tree_digest(files: list[dict[str, str]]) -> str:
    canonical = "".join(
        f"{entry['path']}\0{entry['mode']}\0{entry['sha256']}\n"
        for entry in sorted(files, key=lambda item: item["path"])
    )
    return digest(canonical.encode("utf-8"))


class BaselineFixture:
    def __init__(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "src").mkdir()
        (self.root / "src" / "tracked.txt").write_bytes(b"official\n")
        executable = self.root / "run.sh"
        executable.write_bytes(b"#!/bin/sh\n")
        executable.chmod(0o755)
        (self.root / "poison.txt").write_bytes(b"product\n")
        self.lock_path = self.root / "baseline.lock.json"
        self.files = [
            {
                "path": "poison.txt",
                "classification": "poison-added",
                "mode": "100644",
                "sha256": digest(b"product\n"),
            },
            {
                "path": "run.sh",
                "classification": "upstream",
                "mode": "100755",
                "sha256": digest(b"#!/bin/sh\n"),
            },
            {
                "path": "src/tracked.txt",
                "classification": "upstream",
                "mode": "100644",
                "sha256": digest(b"official\n"),
            },
        ]
        self.lock = {
            "schema": "poison.baseline/v1",
            "upstream": "flipperdevices/flipperzero-firmware",
            "commit": EXPECTED_COMMIT,
            "sourcePath": "do_not_include/flipperzero-firmware",
            "dependencyMode": "resolved-by-ADR-0003",
            "upstreamTreeSha256": tree_digest(self.files),
            "files": self.files,
            "dependencies": [
                {"path": path, "commit": commit}
                for path, commit in sorted(EXPECTED_DEPENDENCIES.items())
            ],
        }
        self.write_lock()

    def write_lock(self) -> None:
        self.lock_path.write_text(
            json.dumps(self.lock, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )

    def close(self) -> None:
        self.temporary.cleanup()


class VerifyBaselineTests(unittest.TestCase):
    def setUp(self) -> None:
        self.fixture = BaselineFixture()

    def tearDown(self) -> None:
        self.fixture.close()

    def verify(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                os.fspath(VERIFIER),
                "--root",
                os.fspath(self.fixture.root),
                "--lock",
                os.fspath(self.fixture.lock_path),
            ],
            capture_output=True,
            check=False,
            text=True,
        )

    def assert_rejected(self, expected_message: str) -> None:
        result = self.verify()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(expected_message, result.stderr)

    def test_accepts_an_exact_materialized_tree(self) -> None:
        result = self.verify()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "baseline verification passed\n")

    def test_rejects_changed_content(self) -> None:
        (self.fixture.root / "src" / "tracked.txt").write_bytes(b"changed\n")
        self.assert_rejected("content mismatch: src/tracked.txt")

    def test_rejects_missing_path(self) -> None:
        (self.fixture.root / "src" / "tracked.txt").unlink()
        self.assert_rejected("missing path: src/tracked.txt")

    def test_rejects_extra_path(self) -> None:
        (self.fixture.root / "extra.txt").write_bytes(b"extra\n")
        self.assert_rejected("extra path: extra.txt")

    def test_ignores_downloaded_toolchain_files(self) -> None:
        tool = self.fixture.root / "toolchain" / "current" / "bin" / "compiler"
        tool.parent.mkdir(parents=True)
        tool.write_bytes(b"generated host tool\n")
        result = self.verify()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_ignores_nested_git_pointer_files(self) -> None:
        git_pointer = self.fixture.root / "src" / "dependency" / ".git"
        git_pointer.parent.mkdir(parents=True)
        git_pointer.write_bytes(b"gitdir: /comparison/repository\n")
        result = self.verify()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_changed_mode(self) -> None:
        (self.fixture.root / "run.sh").chmod(0o644)
        self.assert_rejected("mode mismatch: run.sh")

    def test_rejects_invalid_tree_digest(self) -> None:
        self.fixture.lock["upstreamTreeSha256"] = "0" * 64
        self.fixture.write_lock()
        self.assert_rejected("upstreamTreeSha256 mismatch")

    def test_rejects_incomplete_dependency_pins(self) -> None:
        self.fixture.lock["dependencies"].pop()
        self.fixture.write_lock()
        self.assert_rejected("dependencies must contain exactly 12 unique paths")


class RepositoryBaselineContractTests(unittest.TestCase):
    def test_repository_lock_names_the_official_commit_and_all_dependency_pins(self) -> None:
        lock = json.loads(BASELINE_LOCK.read_text(encoding="utf-8"))
        self.assertEqual(lock["schema"], "poison.baseline/v1")
        self.assertEqual(lock["commit"], EXPECTED_COMMIT)
        self.assertEqual(lock["dependencyMode"], "resolved-by-ADR-0003")
        pins = {entry["path"]: entry["commit"] for entry in lock["dependencies"]}
        self.assertEqual(pins, EXPECTED_DEPENDENCIES)

    def test_repository_lock_records_poisoned_only_paths(self) -> None:
        lock = json.loads(BASELINE_LOCK.read_text(encoding="utf-8"))
        poison_paths = {
            entry["path"]
            for entry in lock["files"]
            if entry["classification"] == "poison-added"
        }
        self.assertIn("README.md", poison_paths)
        self.assertIn("docs/specs/SPEC-1-poisonedos-for-the-flipper-zero.md", poison_paths)
        self.assertIn("docs/plans/2026-08-21-m0-baseline-and-governance.md", poison_paths)

    def test_every_locked_path_is_tracked_by_the_product_repository(self) -> None:
        lock = json.loads(BASELINE_LOCK.read_text(encoding="utf-8"))
        locked_paths = {entry["path"] for entry in lock["files"]}
        tracked = set(
            subprocess.check_output(
                ["git", "-C", os.fspath(REPOSITORY_ROOT), "ls-files"], text=True
            ).splitlines()
        )
        self.assertEqual(locked_paths - tracked, set())

    def test_downloaded_toolchain_ignore_does_not_hide_source_scripts(self) -> None:
        generated = subprocess.run(
            [
                "git",
                "-C",
                os.fspath(REPOSITORY_ROOT),
                "check-ignore",
                "--no-index",
                "toolchain/arm64-darwin/VERSION",
            ],
            capture_output=True,
            check=False,
            text=True,
        )
        source = subprocess.run(
            [
                "git",
                "-C",
                os.fspath(REPOSITORY_ROOT),
                "check-ignore",
                "--no-index",
                "scripts/toolchain/fbtenv.sh",
            ],
            capture_output=True,
            check=False,
            text=True,
        )
        self.assertEqual(generated.returncode, 0)
        self.assertEqual(source.returncode, 1)


if __name__ == "__main__":
    unittest.main()
