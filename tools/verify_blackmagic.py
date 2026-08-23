#!/usr/bin/env python3

import argparse
import hashlib
import json
import re
import tarfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOCK = ROOT / "provenance" / "blackmagic.lock.json"
EXPECTED_REPOSITORY = "flipperdevices/blackmagic-esp32-s2"
SHA256 = re.compile(r"^[0-9a-f]{64}$")
COMMIT = re.compile(r"^[0-9a-f]{40}$")


class BlackmagicVerificationError(RuntimeError):
    pass


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_lock(path=DEFAULT_LOCK):
    try:
        lock = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BlackmagicVerificationError(f"cannot read Blackmagic lock: {error}") from error

    required = (
        "schemaVersion",
        "kind",
        "sourceRepository",
        "sourceCommit",
        "sourceTag",
        "releaseIndexUrl",
        "releaseArtifactUrl",
        "releaseArtifactSha256",
        "releaseArtifactMember",
        "bundledImagePath",
        "bundledImageSize",
        "bundledImageSha256",
        "imageMetadata",
        "requiredCapabilities",
    )
    missing = [field for field in required if field not in lock]
    if missing:
        raise BlackmagicVerificationError(f"Blackmagic lock missing fields: {', '.join(missing)}")
    if lock["schemaVersion"] != 1 or lock["kind"] != "poison-wifi-board-runtime":
        raise BlackmagicVerificationError("unsupported Blackmagic lock schema")
    if lock["sourceRepository"] != EXPECTED_REPOSITORY:
        raise BlackmagicVerificationError("unexpected Blackmagic source repository")
    if not COMMIT.fullmatch(lock["sourceCommit"]):
        raise BlackmagicVerificationError("invalid Blackmagic source commit")
    for field in ("releaseArtifactSha256", "bundledImageSha256"):
        if not SHA256.fullmatch(lock[field]):
            raise BlackmagicVerificationError(f"invalid {field}")
    if not isinstance(lock["bundledImageSize"], int) or lock["bundledImageSize"] <= 0:
        raise BlackmagicVerificationError("invalid bundledImageSize")
    if Path(lock["releaseArtifactMember"]).name != lock["releaseArtifactMember"]:
        raise BlackmagicVerificationError("releaseArtifactMember must be a base name")

    capabilities = lock["requiredCapabilities"]
    expected_capabilities = {
        "rawTcpUartPort": 3456,
        "maxRawTcpClients": 1,
        "uartGetConfigPath": "/api/v1/uart/get_config",
        "uartSetConfigPath": "/api/v1/uart/set_config",
    }
    if capabilities != expected_capabilities:
        raise BlackmagicVerificationError("Blackmagic runtime capabilities do not match the supported control plane")
    return lock


def bundled_image_path(lock, root=ROOT):
    root = Path(root).resolve()
    image = (root / lock["bundledImagePath"]).resolve()
    if image != root and root not in image.parents:
        raise BlackmagicVerificationError("bundledImagePath escapes the repository")
    return image


def verify_bundled_image(lock, root=ROOT):
    image = bundled_image_path(lock, root)
    try:
        size = image.stat().st_size
    except OSError as error:
        raise BlackmagicVerificationError(f"cannot stat bundled Blackmagic image: {error}") from error
    if size != lock["bundledImageSize"]:
        raise BlackmagicVerificationError(
            f"bundled Blackmagic image size mismatch: {size} != {lock['bundledImageSize']}"
        )
    digest = sha256_file(image)
    if digest != lock["bundledImageSha256"]:
        raise BlackmagicVerificationError(
            f"bundled Blackmagic image SHA-256 mismatch: {digest} != {lock['bundledImageSha256']}"
        )
    return image


def verify_release_archive(lock, archive_path):
    archive_path = Path(archive_path)
    digest = sha256_file(archive_path)
    if digest != lock["releaseArtifactSha256"]:
        raise BlackmagicVerificationError(
            f"Blackmagic release archive SHA-256 mismatch: {digest} != {lock['releaseArtifactSha256']}"
        )

    expected_member = lock["releaseArtifactMember"]
    try:
        with tarfile.open(archive_path, "r:gz") as archive:
            matches = [
                member
                for member in archive.getmembers()
                if member.isfile() and member.name.removeprefix("./") == expected_member
            ]
            if len(matches) != 1:
                raise BlackmagicVerificationError(
                    f"Blackmagic release archive must contain exactly one {expected_member}"
                )
            source = archive.extractfile(matches[0])
            if source is None:
                raise BlackmagicVerificationError("cannot read Blackmagic release image")
            payload = source.read()
    except (OSError, tarfile.TarError) as error:
        raise BlackmagicVerificationError(f"cannot read Blackmagic release archive: {error}") from error

    if len(payload) != lock["bundledImageSize"]:
        raise BlackmagicVerificationError("Blackmagic release image size does not match bundled image")
    if hashlib.sha256(payload).hexdigest() != lock["bundledImageSha256"]:
        raise BlackmagicVerificationError("Blackmagic release image does not match bundled image")


def build_parser():
    parser = argparse.ArgumentParser(description="Verify the pinned Poisoned_Os Wi-Fi-board runtime")
    parser.add_argument("--lock", type=Path, default=DEFAULT_LOCK)
    parser.add_argument("--archive", type=Path)
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    lock = load_lock(args.lock)
    image = verify_bundled_image(lock)
    if args.archive:
        verify_release_archive(lock, args.archive)
    print(
        f"verified Blackmagic {lock['sourceTag']} at {lock['sourceCommit'][:12]}: "
        f"{image.relative_to(ROOT)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
