import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("package_platforms", ROOT / "tools/release/package_platforms.py")
MODULE = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(MODULE)


class PackagePlatformsTests(unittest.TestCase):
    def test_packages_supported_platforms_deterministically(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); source = root / "release.zip"; source.write_bytes(b"release")
            first = MODULE.package(source, root / "one", ["windows-x64", "macos-arm64"])
            second = MODULE.package(source, root / "two", ["macos-arm64", "windows-x64"])
            self.assertEqual(first, second)

    def test_rejects_unknown_platform(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); source = root / "release.zip"; source.write_bytes(b"release")
            with self.assertRaises(ValueError): MODULE.package(source, root / "out", ["plan9-x64"])
