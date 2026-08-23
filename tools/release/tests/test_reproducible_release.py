import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("build_release", ROOT / "tools/release/build_release.py")
MODULE = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(MODULE)


class ReproducibleReleaseTests(unittest.TestCase):
    def test_repeated_assembly_is_byte_identical(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); inputs = root / "inputs"; inputs.mkdir(); (inputs / "firmware.bin").write_bytes(b"firmware"); (inputs / "schemas.json").write_text("{}", encoding="utf-8")
            first, second = root / "a.zip", root / "b.zip"
            MODULE.build(inputs, first, "1.0.0", "stable", "f7", "0.9.0"); MODULE.build(inputs, second, "1.0.0", "stable", "f7", "0.9.0")
            self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_empty_or_invalid_release_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError): MODULE.build(Path(directory), Path(directory) / "out.zip", "1.0.0", "stable", "f7", "0.9.0")
