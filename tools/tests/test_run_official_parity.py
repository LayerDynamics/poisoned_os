from __future__ import annotations

import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = REPOSITORY_ROOT / "tools" / "run_official_parity.py"
SPEC = importlib.util.spec_from_file_location("run_official_parity", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("cannot load official parity module")
PARITY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PARITY)


def write_api(path: Path, version: str, extra: tuple[str, ...] | None = None) -> None:
    rows = [
        ("entry", "status", "name", "type", "params"),
        ("Version", "+", version, "", ""),
        ("Header", "+", "applications/services/gui/modules/menu.h", "", ""),
        ("Function", "+", "existing_api", "void", "uint32_t"),
    ]
    if extra is not None:
        rows.append(extra)
    with path.open("w", encoding="utf-8", newline="") as stream:
        csv.writer(stream, lineterminator="\n").writerows(rows)


class OfficialParityTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_accepts_only_the_locked_api_version_and_menu_addition(self) -> None:
        baseline = self.root / "baseline.csv"
        product = self.root / "product.csv"
        write_api(baseline, "88.2")
        write_api(
            product,
            "88.3",
            ("Function", "+", "menu_set_header", "void", "Menu*, const char*"),
        )

        classification = PARITY.compare_api_tables(product, baseline)

        self.assertEqual(
            classification,
            {
                "baseline_version": "88.2",
                "product_version": "88.3",
                "approved_additions": ["Function:menu_set_header"],
            },
        )

    def test_rejects_an_unexplained_api_addition(self) -> None:
        baseline = self.root / "baseline.csv"
        product = self.root / "product.csv"
        write_api(baseline, "88.2")
        write_api(product, "88.3", ("Function", "+", "surprise", "void", ""))

        with self.assertRaisesRegex(
            PARITY.OfficialParityError, "unexplained API addition"
        ):
            PARITY.compare_api_tables(product, baseline)

    def test_accepts_only_the_product_origin_and_suffix_metadata_changes(self) -> None:
        baseline = self.root / "baseline_options.py"
        product = self.root / "product_options.py"
        common = "\nTARGET_HW = 7\nDEBUG = 1\n"
        baseline.write_text(
            'FIRMWARE_ORIGIN = "Official"\nDIST_SUFFIX = "local"' + common,
            encoding="utf-8",
        )
        product.write_text(
            'FIRMWARE_ORIGIN = "PoisonedOS"\nDIST_SUFFIX = "poisonedos"' + common,
            encoding="utf-8",
        )

        classification = PARITY.compare_build_options(product, baseline)

        self.assertEqual(
            classification,
            {
                "FIRMWARE_ORIGIN": {"baseline": "Official", "product": "PoisonedOS"},
                "DIST_SUFFIX": {"baseline": "local", "product": "poisonedos"},
            },
        )

    def test_rejects_an_unexplained_build_option_change(self) -> None:
        baseline = self.root / "baseline_options.py"
        product = self.root / "product_options.py"
        baseline.write_text(
            'FIRMWARE_ORIGIN = "Official"\nDIST_SUFFIX = "local"\nTARGET_HW = 7\n',
            encoding="utf-8",
        )
        product.write_text(
            'FIRMWARE_ORIGIN = "PoisonedOS"\nDIST_SUFFIX = "poisonedos"\nTARGET_HW = 8\n',
            encoding="utf-8",
        )

        with self.assertRaisesRegex(
            PARITY.OfficialParityError, "unexplained fbt_options"
        ):
            PARITY.compare_build_options(product, baseline)

    def test_validates_real_artifact_formats_and_target(self) -> None:
        product = self.root / "product"
        baseline = self.root / "baseline"
        self._write_artifacts(product, b"product")
        self._write_artifacts(baseline, b"baseline")

        report = PARITY.compare_artifacts(product, baseline)

        self.assertEqual(set(report), set(PARITY.ARTIFACT_SPECS))
        self.assertEqual(report["firmware_json"]["classification"], "build-metadata")
        self.assertEqual(
            report["firmware_dfu"]["classification"], "product-source-delta"
        )

    def test_rejects_bad_artifact_magic_and_target(self) -> None:
        product = self.root / "product"
        baseline = self.root / "baseline"
        self._write_artifacts(product, b"product")
        self._write_artifacts(baseline, b"baseline")
        (product / "build/f7-firmware-D/firmware.dfu").write_bytes(b"not a dfu")

        with self.assertRaisesRegex(PARITY.OfficialParityError, "invalid DFU magic"):
            PARITY.compare_artifacts(product, baseline)

        self._write_artifacts(product, b"product", target=8)
        with self.assertRaisesRegex(
            PARITY.OfficialParityError, "firmware target mismatch"
        ):
            PARITY.compare_artifacts(product, baseline)

    def test_requires_a_complete_source_classification_report(self) -> None:
        report = self.root / "upstream-paths.json"
        report.write_text(
            json.dumps(
                {
                    "baselineCommit": "locked-commit",
                    "paths": [
                        {"path": "same.c", "classification": "identical"},
                        {"path": "product.c", "classification": "poison-modified"},
                    ],
                }
            ),
            encoding="utf-8",
        )
        counts = PARITY.validate_source_report(report, "locked-commit")
        self.assertEqual(counts, {"identical": 1, "poison-modified": 1})

        value = json.loads(report.read_text(encoding="utf-8"))
        value["paths"][1]["classification"] = "unknown"
        report.write_text(json.dumps(value), encoding="utf-8")
        with self.assertRaisesRegex(
            PARITY.OfficialParityError, "invalid source classification"
        ):
            PARITY.validate_source_report(report, "locked-commit")

    @staticmethod
    def _write_artifacts(directory: Path, marker: bytes, target: int = 7) -> None:
        directory.mkdir(parents=True, exist_ok=True)
        values = {
            "build/f7-firmware-D/firmware.bin": b"\0\0\x03\x20" + marker,
            "build/f7-firmware-D/firmware.dfu": b"DfuSe\x01" + marker,
            "build/f7-firmware-D/firmware.elf": b"\x7fELF" + marker,
            "build/f7-updater-D/updater.bin": b"\0\0\x03\x20" + marker,
            "build/f7-updater-D/updater.dfu": b"DfuSe\x01" + marker,
            "build/f7-updater-D/updater.elf": b"\x7fELF" + marker,
        }
        for name, value in values.items():
            path = directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(value)
        metadata = {
            "firmware_build_date": "21-08-2026",
            "firmware_commit": marker.decode("ascii")[:8],
            "firmware_branch": "main",
            "firmware_target": target,
        }
        for component, filename in (
            ("firmware", "firmware.json"),
            ("updater", "updater.json"),
        ):
            path = directory / f"build/f7-{component}-D" / filename
            path.write_text(json.dumps(metadata), encoding="utf-8")


if __name__ == "__main__":
    unittest.main()
