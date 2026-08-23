import hashlib
import importlib.util
import io
import json
import tarfile
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "verify_blackmagic.py"


def load_module():
    spec = importlib.util.spec_from_file_location("verify_blackmagic", MODULE_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class BlackmagicVerificationTest(unittest.TestCase):
    def setUp(self):
        self.module = load_module()

    def test_repository_image_matches_the_pinned_official_release(self):
        lock = self.module.load_lock()
        image = self.module.verify_bundled_image(lock)
        self.assertEqual(image.stat().st_size, 961488)
        self.assertEqual(
            self.module.sha256_file(image),
            "89386c52b34a9112f7c0c445fdd7e48ce9ceac05aae52c9a7d2bfb820d675f1d",
        )

    def test_tampered_bundled_image_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = root / "blackmagic.bin"
            image.write_bytes(b"official-image")
            lock = self.module.load_lock()
            lock["bundledImagePath"] = image.name
            lock["bundledImageSize"] = image.stat().st_size
            lock["bundledImageSha256"] = hashlib.sha256(b"different").hexdigest()
            with self.assertRaisesRegex(self.module.BlackmagicVerificationError, "SHA-256"):
                self.module.verify_bundled_image(lock, root)

    def test_release_archive_must_match_both_archive_and_image_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload = b"official-blackmagic-image"
            archive_path = root / "release.tgz"
            with tarfile.open(archive_path, "w:gz") as archive:
                member = tarfile.TarInfo("./blackmagic.bin")
                member.size = len(payload)
                archive.addfile(member, io.BytesIO(payload))

            lock = self.module.load_lock()
            lock["releaseArtifactSha256"] = self.module.sha256_file(archive_path)
            lock["bundledImageSize"] = len(payload)
            lock["bundledImageSha256"] = hashlib.sha256(payload).hexdigest()
            self.module.verify_release_archive(lock, archive_path)

            lock["releaseArtifactSha256"] = hashlib.sha256(b"tampered").hexdigest()
            with self.assertRaisesRegex(self.module.BlackmagicVerificationError, "archive SHA-256"):
                self.module.verify_release_archive(lock, archive_path)

    def test_lock_rejects_a_changed_control_plane_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "blackmagic.lock.json"
            lock = self.module.load_lock()
            lock["requiredCapabilities"]["rawTcpUartPort"] = 9999
            path.write_text(json.dumps(lock), encoding="utf-8")
            with self.assertRaisesRegex(self.module.BlackmagicVerificationError, "capabilities"):
                self.module.load_lock(path)


if __name__ == "__main__":
    unittest.main()
