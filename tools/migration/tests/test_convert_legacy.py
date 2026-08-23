import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("convert_legacy", ROOT / "tools/migration/convert_legacy.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ConvertLegacyTests(unittest.TestCase):
    def test_inventory_and_stage_are_deterministic_and_byte_preserving(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            backup = root / "backup"
            target = root / "target"
            (source / "ext/subghz").mkdir(parents=True)
            (source / "int/scripts").mkdir(parents=True)
            (source / "ext/subghz/test.sub").write_bytes(b"subghz")
            (source / "int/scripts/demo.js").write_bytes(b"console.log(1)\n")
            manifest = MODULE.inventory_legacy(source, backup)
            self.assertEqual([entry["sourcePath"] for entry in manifest["entries"]], ["/int/scripts/demo.js", "/ext/subghz/test.sub"])
            staged = MODULE.stage_manifest(manifest, source, backup, target)
            self.assertEqual(staged, ["/scripts/demo.js", "/captures/subghz/test.sub"])
            self.assertEqual((target / "captures/subghz/test.sub").read_bytes(), b"subghz")
            self.assertEqual((source / "ext/subghz/test.sub").read_bytes(), (backup / "legacy/ext/subghz/test.sub").read_bytes())

    def test_tampered_backup_blocks_staging(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            backup = root / "backup"
            (source / "ext").mkdir(parents=True)
            (source / "ext/test.sub").write_bytes(b"source")
            manifest = MODULE.inventory_legacy(source, backup)
            (backup / "legacy/ext/test.sub").write_bytes(b"tampered")
            with self.assertRaises(ValueError):
                MODULE.stage_manifest(manifest, source, backup, root / "target")


if __name__ == "__main__":
    unittest.main()
