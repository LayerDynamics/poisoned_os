import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "release"))


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / "release" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


DIST = load("build_web_installer_distribution")


class WebInstallerDistributionTests(unittest.TestCase):
    def test_assembles_signed_manifest_feed_config_and_package(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "dist" / "f7-C" / "flipper-z-f7-update-poisonedos.tgz"
            package.parent.mkdir(parents=True)
            package.write_bytes(b"real target-7 update package")
            private = root / "private.pem"
            subprocess.run(
                ["openssl", "genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-out", str(private)],
                check=True,
                capture_output=True,
            )
            output = root / "web-installer" / "dist"
            manifest = DIST.assemble(
                root,
                package,
                output,
                "1.0.0",
                "developer",
                "railway-firmware-1",
                private.read_bytes(),
            )
            self.assertEqual(manifest["signature"]["keyId"], "railway-firmware-1")
            self.assertTrue((output / "releases.json").is_file())
            self.assertTrue((output / "release.json").is_file())
            self.assertTrue((output / "installer-config.json").is_file())
            self.assertTrue((output / "releases" / "1.0.0" / package.name).is_file())
            config = json.loads((output / "installer-config.json").read_text(encoding="utf-8"))
            self.assertEqual(config["releaseFeedUrl"], "./releases.json")
            self.assertIn("railway-firmware-1", config["trustedReleaseKeys"])

    def test_rejects_missing_or_invalid_signer(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / "update.tgz"
            package.write_bytes(b"package")
            with self.assertRaisesRegex(DIST.DistributionError, "P-256"):
                DIST.assemble(root, package, root / "out", "1.0.0", "developer", "railway-firmware-1", b"not-a-key")


if __name__ == "__main__":
    unittest.main()
