import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]


def load(name):
    spec = importlib.util.spec_from_file_location(
        name, ROOT / "tools" / "release" / f"{name}.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


FEED = load("build_web_installer_feed")
SIGN = load("sign_release")


class WebInstallerFeedTests(unittest.TestCase):
    def fixture(self, directory: str):
        root = Path(directory)
        package = root / "dist" / "flipper-z-f7-update-poisonedos.tgz"
        package.parent.mkdir()
        package.write_bytes(b"real update tgz bytes")
        manifest = root / "release.json"
        manifest.write_text(
            json.dumps(
                {
                    "schema": "poison.release-manifest/v1",
                    "version": "1.2.3",
                    "channel": "stable",
                    "target": "f7",
                    "rollbackVersion": "1.2.2",
                    "components": [
                        {
                            "id": "firmware.update.tgz",
                            "path": package.relative_to(root).as_posix(),
                            "sha256": hashlib.sha256(package.read_bytes()).hexdigest(),
                            "bytes": package.stat().st_size,
                        }
                    ],
                    "revocations": [],
                }
            ),
            encoding="utf-8",
        )
        private_key = root / "private.pem"
        public_key = root / "public.pem"
        subprocess.run(
            [
                "openssl",
                "genpkey",
                "-algorithm",
                "EC",
                "-pkeyopt",
                "ec_paramgen_curve:P-256",
                "-out",
                str(private_key),
            ],
            check=True,
            capture_output=True,
        )
        subprocess.run(
            [
                "openssl",
                "pkey",
                "-in",
                str(private_key),
                "-pubout",
                "-out",
                str(public_key),
            ],
            check=True,
            capture_output=True,
        )
        SIGN.sign(manifest, private_key, "firmware-test-1")
        return root, manifest, package, public_key

    def test_builds_feed_only_after_signature_and_package_digest_verification(self):
        with tempfile.TemporaryDirectory() as directory:
            root, manifest, package, public_key = self.fixture(directory)
            feed = FEED.build_feed(
                root,
                [(manifest, package, "releases/update.tgz")],
                {"firmware-test-1": public_key},
            )

        self.assertEqual(feed["schema"], "poison.web-installer-feed/v1")
        self.assertEqual(len(feed["releases"]), 1)
        release = feed["releases"][0]
        self.assertEqual(release["packageComponentId"], "firmware.update.tgz")
        self.assertEqual(release["packageUrl"], "releases/update.tgz")
        self.assertEqual(
            release["manifest"]["signature"]["algorithm"],
            "ECDSA-P256-SHA256",
        )

    def test_rejects_manifest_tampering_after_signature(self):
        with tempfile.TemporaryDirectory() as directory:
            root, manifest, package, public_key = self.fixture(directory)
            changed = json.loads(manifest.read_text(encoding="utf-8"))
            changed["version"] = "9.9.9"
            manifest.write_text(json.dumps(changed), encoding="utf-8")
            with self.assertRaisesRegex(FEED.FeedError, "signature is invalid"):
                FEED.build_feed(
                    root,
                    [(manifest, package, "releases/update.tgz")],
                    {"firmware-test-1": public_key},
                )

    def test_rejects_package_bytes_that_do_not_match_signed_component(self):
        with tempfile.TemporaryDirectory() as directory:
            root, manifest, package, public_key = self.fixture(directory)
            package.write_bytes(b"tampered update")
            with self.assertRaisesRegex(FEED.FeedError, "does not match"):
                FEED.build_feed(
                    root,
                    [(manifest, package, "releases/update.tgz")],
                    {"firmware-test-1": public_key},
                )

    def test_rejects_untrusted_release_signer(self):
        with tempfile.TemporaryDirectory() as directory:
            root, manifest, package, _public_key = self.fixture(directory)
            with self.assertRaisesRegex(FEED.FeedError, "no trusted public key"):
                FEED.build_feed(
                    root,
                    [(manifest, package, "releases/update.tgz")],
                    {},
                )

    def test_rejects_insecure_absolute_package_url(self):
        with tempfile.TemporaryDirectory() as directory:
            root, manifest, package, public_key = self.fixture(directory)
            with self.assertRaisesRegex(FEED.FeedError, "must use HTTPS"):
                FEED.build_feed(
                    root,
                    [(manifest, package, "http://downloads.example/update.tgz")],
                    {"firmware-test-1": public_key},
                )

    def test_rejects_a_manifest_the_browser_would_reject(self):
        with tempfile.TemporaryDirectory() as directory:
            root, manifest, package, public_key = self.fixture(directory)
            changed = json.loads(manifest.read_text(encoding="utf-8"))
            changed["version"] = "1.2"
            manifest.write_text(json.dumps(changed), encoding="utf-8")
            with self.assertRaisesRegex(FEED.FeedError, "invalid version"):
                FEED.build_feed(
                    root,
                    [(manifest, package, "releases/update.tgz")],
                    {"firmware-test-1": public_key},
                )


if __name__ == "__main__":
    unittest.main()
