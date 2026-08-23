import datetime
import os
import subprocess
from functools import cache


@cache
def get_git_commit_unix_timestamp():
    try:
        return int(subprocess.check_output(["git", "show", "-s", "--format=%ct"]))
    except (OSError, subprocess.CalledProcessError, ValueError):
        return int(os.environ.get("SOURCE_DATE_EPOCH", "0"))


@cache
def get_fast_git_version_id():
    try:
        version = (
            subprocess.check_output(
                [
                    "git",
                    "describe",
                    "--always",
                    "--dirty",
                    "--all",
                    "--long",
                ]
            )
            .strip()
            .decode()
        )
        return (version, datetime.date.today())
    except (OSError, subprocess.CalledProcessError) as error:
        print("Failed to check for git changes", error)
        return (
            os.environ.get("RAILWAY_GIT_COMMIT_SHA", "unknown"),
            datetime.date.today(),
        )
