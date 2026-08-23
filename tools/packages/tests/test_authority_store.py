from __future__ import annotations

import base64
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


def load():
    path = ROOT / "tools/packages/build_authority_store.py"
    spec = importlib.util.spec_from_file_location("build_authority_store", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class AuthorityStoreTests(unittest.TestCase):
    PUBLIC_KEY = bytes.fromhex(
        "04d0b135779ff26693c4cd257a412dbf45ad3f2d48be2f3d25a17fd5a63ba0eb"
        "4dce42c89ee54242981bf5fc110be5053f07c31a97e32e9d9ab6f2b2f066966675"
    )

    def test_encodes_firmware_compatible_sorted_store(self):
        module = load()
        encoded = module.encode([
            module.Authority("update-prod-1", self.PUBLIC_KEY, False),
            module.Authority("update-revoked-1", self.PUBLIC_KEY, True),
        ])
        self.assertEqual(encoded[:8], b"PPK1\x01\x02\x00\x00")
        self.assertEqual(encoded[8], len("update-prod-1"))
        self.assertEqual(encoded[9], 0)
        self.assertEqual(encoded[12:77], self.PUBLIC_KEY)
        self.assertIn(b"update-revoked-1", encoded)

    def test_rejects_duplicate_ids(self):
        module = load()
        with self.assertRaises(module.AuthorityStoreError):
            module.encode([
                module.Authority("duplicate", self.PUBLIC_KEY),
                module.Authority("duplicate", self.PUBLIC_KEY),
            ])

    def test_parses_p256_subject_public_key(self):
        module = load()
        prefix = bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d030107034200")
        der = prefix + self.PUBLIC_KEY
        body = base64.b64encode(der).decode("ascii")
        pem = "-----BEGIN PUBLIC KEY-----\n" + "\n".join(
            body[index:index + 64] for index in range(0, len(body), 64)
        ) + "\n-----END PUBLIC KEY-----\n"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "public.pem"
            path.write_text(pem, encoding="ascii")
            self.assertEqual(module.parse_public_key(path), self.PUBLIC_KEY)


if __name__ == "__main__":
    unittest.main()
