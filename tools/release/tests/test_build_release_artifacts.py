import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "release"))
SPEC = importlib.util.spec_from_file_location("build_release_artifacts", ROOT / "tools/release/build_release_artifacts.py")
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class BuildReleaseArtifactsTests(unittest.TestCase):
    def test_builds_distribution_after_firmware_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "dist" / "f7-C" / "flipper-z-f7-update-poisonedos.tgz"
            package.parent.mkdir(parents=True)
            package.write_bytes(b"firmware-package")
            key = subprocess.check_output([
                "openssl", "genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256",
            ])
            output = root / "output"
            with patch.object(MODULE, "_run_firmware_build", return_value=package) as build:
                manifest = MODULE.build_release(root, output, "1.2.3", "developer", "test-key-1", key)
            build.assert_called_once()
            self.assertEqual(manifest["version"], "1.2.3")
            self.assertTrue((output / "releases.json").is_file())
            self.assertTrue((output / "installer-config.json").is_file())
            self.assertTrue((output / "build-info.json").is_file())

    def test_firmware_builder_uses_reproducible_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "dist" / "f7-C" / "flipper-z-f7-update-poisonedos.tgz"
            package.parent.mkdir(parents=True)
            package.write_bytes(b"firmware-package")
            with patch.object(MODULE.subprocess, "run") as run:
                run.return_value = None
                with patch.object(MODULE, "distribution") as distribution:
                    distribution.VERSION.fullmatch.return_value = True
                    distribution.assemble.return_value = {"version": "1.2.3", "channel": "developer"}
                    MODULE.build_release(root, root / "output", "1.2.3", "developer", "test-key-1", b"key", run_build=True)
            _, kwargs = run.call_args
            self.assertEqual(kwargs["env"]["FBT_NO_SYNC"], "1")
            self.assertEqual(kwargs["env"]["SOURCE_DATE_EPOCH"], "0")


if __name__ == "__main__":
    unittest.main()
