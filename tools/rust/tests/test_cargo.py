import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("cargo_runner", ROOT / "tools/rust/cargo.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
TOOLCHAIN_SPEC = importlib.util.spec_from_file_location(
    "toolchain", ROOT / "tools/rust/toolchain.py"
)
TOOLCHAIN = importlib.util.module_from_spec(TOOLCHAIN_SPEC)
TOOLCHAIN_SPEC.loader.exec_module(TOOLCHAIN)


class CargoRunnerTests(unittest.TestCase):
    def test_reads_numeric_pinned_channel(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "rust-toolchain.toml").write_text('[toolchain]\nchannel = "1.96.0"\n', encoding="utf-8")
            self.assertEqual(MODULE._requested_channel(root), "1.96.0")

    def test_rejects_unpinned_channel(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "rust-toolchain.toml").write_text('[toolchain]\nchannel = "stable"\n', encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "numeric"):
                MODULE._requested_channel(root)

    def test_metadata_sync_is_skipped_for_explicit_toolchain(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            environment = {"RUSTUP_TOOLCHAIN": "custom"}
            TOOLCHAIN.sync_metadata(root, environment, Path("/missing/cargo"))
            self.assertFalse((root / "build" / "toolchain-metadata.json").exists())

    def test_unconfigured_environment_syncs_and_selects_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "rust-toolchain.toml").write_text(
                '[toolchain]\nchannel = "1.96.0"\n', encoding="utf-8"
            )
            cargo = root / "rustup" / "toolchains" / "1.96.0-aarch64" / "bin" / "cargo"
            cargo.parent.mkdir(parents=True)
            cargo.touch()
            cargo.with_name("rustc").touch()
            environment = {"RUSTUP_HOME": str(root / "rustup")}
            completed = mock.Mock(stdout="release: 1.96.0\nhost: aarch64\n")
            targets = mock.Mock(stdout="thumbv7em-none-eabihf\n")
            with mock.patch.object(TOOLCHAIN.subprocess, "run", side_effect=[completed, targets]):
                selected = TOOLCHAIN.prepare(root, environment)
            self.assertEqual(selected, cargo)
            self.assertEqual(environment["RUSTUP_TOOLCHAIN"], "1.96.0-aarch64")
            metadata = (root / "build" / "toolchain-metadata.json").read_text()
            self.assertIn("poison.rust-toolchain-metadata/v1", metadata)
