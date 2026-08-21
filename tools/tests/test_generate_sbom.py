from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
GENERATOR = REPOSITORY_ROOT / "tools" / "generate_sbom.py"


def subtree_digest(root: Path) -> str:
    records: list[str] = []
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        relative = path.relative_to(root).as_posix()
        mode = "100755" if path.stat().st_mode & 0o111 else "100644"
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        records.append(f"{relative}\0{mode}\0{digest}\n")
    return hashlib.sha256("".join(records).encode("utf-8")).hexdigest()


class SbomFixture:
    def __init__(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.component = self.root / "component"
        self.component.mkdir()
        (self.component / "source.c").write_bytes(b"int answer = 42;\n")
        self.components_path = self.root / "components.json"
        self.licenses_path = self.root / "licenses.json"
        self.output = self.root / "output"
        self.components = {
            "schema": "poison.components/v1",
            "components": [
                {
                    "name": "fixture-component",
                    "path": "component",
                    "version": "1.0.0",
                    "sha256": subtree_digest(self.component),
                    "licenseExpression": "MIT",
                    "licenseIds": ["MIT"],
                    "sourceUrl": "https://example.invalid/fixture-component",
                    "modificationStatus": "unmodified",
                }
            ],
        }
        self.licenses = {
            "schema": "poison.licenses/v1",
            "licenses": [
                {
                    "id": "MIT",
                    "name": "MIT License",
                    "redistributionAllowed": True,
                    "standard": True,
                    "evidencePaths": ["component/source.c"],
                }
            ],
        }
        self.write_manifests()

    def write_manifests(self) -> None:
        self.components_path.write_text(
            json.dumps(self.components, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        self.licenses_path.write_text(
            json.dumps(self.licenses, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    def run(self, mode: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                os.fspath(GENERATOR),
                "--root",
                os.fspath(self.root),
                "--components",
                os.fspath(self.components_path),
                "--licenses",
                os.fspath(self.licenses_path),
                "--output-dir",
                os.fspath(self.output),
                mode,
            ],
            capture_output=True,
            check=False,
            text=True,
        )

    def close(self) -> None:
        self.temporary.cleanup()


class GenerateSbomTests(unittest.TestCase):
    def setUp(self) -> None:
        self.fixture = SbomFixture()

    def tearDown(self) -> None:
        self.fixture.close()

    def test_check_accepts_complete_component_and_license_manifests(self) -> None:
        result = self.fixture.run("--check")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "SBOM verification passed: 1 components, 0 unknown licenses, 0 release blockers\n",
        )

    def test_rejects_missing_required_component_field(self) -> None:
        del self.fixture.components["components"][0]["sourceUrl"]
        self.fixture.write_manifests()
        result = self.fixture.run("--check")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing required field: sourceUrl", result.stderr)

    def test_rejects_unknown_license_id(self) -> None:
        component = self.fixture.components["components"][0]
        component["licenseIds"] = ["LicenseRef-Unknown"]
        component["licenseExpression"] = "LicenseRef-Unknown"
        self.fixture.write_manifests()
        result = self.fixture.run("--check")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unknown license: LicenseRef-Unknown", result.stderr)

    def test_rejects_changed_component_bytes(self) -> None:
        (self.fixture.component / "source.c").write_bytes(b"changed\n")
        result = self.fixture.run("--check")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("component digest mismatch: fixture-component", result.stderr)

    def test_reports_component_level_release_blocker(self) -> None:
        component = self.fixture.components["components"][0]
        component["releaseBlocked"] = True
        component["releaseBlockerReason"] = "Incompatible combined-work licenses."
        self.fixture.write_manifests()

        result = self.fixture.run("--check")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "SBOM verification passed: 1 components, 0 unknown licenses, 1 release blockers\n",
        )

    def test_write_produces_deterministic_spdx_and_cyclonedx_json(self) -> None:
        first = self.fixture.run("--write")
        self.assertEqual(first.returncode, 0, first.stderr)
        spdx_path = self.fixture.output / "poisonedos.spdx.json"
        cyclonedx_path = self.fixture.output / "poisonedos.cdx.json"
        first_spdx = spdx_path.read_bytes()
        first_cyclonedx = cyclonedx_path.read_bytes()

        second = self.fixture.run("--write")
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(spdx_path.read_bytes(), first_spdx)
        self.assertEqual(cyclonedx_path.read_bytes(), first_cyclonedx)

        spdx = json.loads(first_spdx)
        cyclonedx = json.loads(first_cyclonedx)
        self.assertEqual(spdx["spdxVersion"], "SPDX-2.3")
        self.assertEqual(spdx["packages"][0]["licenseConcluded"], "MIT")
        self.assertEqual(cyclonedx["bomFormat"], "CycloneDX")
        self.assertEqual(cyclonedx["specVersion"], "1.6")
        self.assertEqual(cyclonedx["components"][0]["licenses"], [{"expression": "MIT"}])

    def test_write_defines_custom_license_references(self) -> None:
        component = self.fixture.components["components"][0]
        component["licenseIds"] = ["LicenseRef-Fixture"]
        component["licenseExpression"] = "LicenseRef-Fixture"
        self.fixture.licenses["licenses"] = [
            {
                "id": "LicenseRef-Fixture",
                "name": "Fixture custom terms",
                "standard": False,
                "redistributionAllowed": False,
                "evidencePaths": ["component/source.c"],
                "extractedText": "No redistribution grant is provided.",
            }
        ]
        self.fixture.write_manifests()

        result = self.fixture.run("--write")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("(1 release blockers)", result.stdout)
        spdx = json.loads((self.fixture.output / "poisonedos.spdx.json").read_bytes())
        cyclonedx = json.loads((self.fixture.output / "poisonedos.cdx.json").read_bytes())
        self.assertEqual(
            spdx["hasExtractedLicensingInfos"],
            [
                {
                    "extractedText": "No redistribution grant is provided.",
                    "licenseId": "LicenseRef-Fixture",
                    "name": "Fixture custom terms",
                }
            ],
        )
        self.assertIn("redistributionAllowed=false", spdx["packages"][0]["comment"])
        self.assertIn("releaseBlocked=true", spdx["packages"][0]["comment"])
        self.assertIn(
            {
                "name": "poisonedos:redistribution-allowed",
                "value": "false",
            },
            cyclonedx["components"][0]["properties"],
        )


if __name__ == "__main__":
    unittest.main()
