import datetime
import os
import subprocess
import unittest
from unittest.mock import patch

from scripts.fbt import version


class FbtVersionFallbackTests(unittest.TestCase):
    def tearDown(self):
        version.get_git_commit_unix_timestamp.cache_clear()
        version.get_fast_git_version_id.cache_clear()

    def test_timestamp_uses_reproducible_epoch_without_git(self):
        with patch.dict(os.environ, {"SOURCE_DATE_EPOCH": "123"}, clear=False), patch(
            "scripts.fbt.version.subprocess.check_output",
            side_effect=subprocess.CalledProcessError(128, ["git"]),
        ):
            self.assertEqual(version.get_git_commit_unix_timestamp(), 123)

    def test_version_id_uses_railway_sha_without_git(self):
        with patch.dict(os.environ, {"RAILWAY_GIT_COMMIT_SHA": "abc123"}, clear=False), patch(
            "scripts.fbt.version.subprocess.check_output",
            side_effect=OSError("git unavailable"),
        ):
            self.assertEqual(version.get_fast_git_version_id(), ("abc123", datetime.date.today()))


if __name__ == "__main__":
    unittest.main()
