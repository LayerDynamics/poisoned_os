from __future__ import annotations

import csv
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SNAPSHOTTER = REPOSITORY_ROOT / "tools" / "snapshot_firmware_api.py"


class FirmwareApiFixture:
    def __init__(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        header = self.root / "include" / "menu.h"
        header.parent.mkdir(parents=True)
        header.write_text(
            "void menu_set_header(Menu* menu, const char* header);\n"
            "extern const unsigned int menu_generation;\n",
            encoding="utf-8",
        )
        self.api = self.root / "api_symbols.csv"
        with self.api.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream, lineterminator="\n")
            writer.writerow(("entry", "status", "name", "type", "params"))
            writer.writerow(("Version", "+", "88.3", "", ""))
            writer.writerow(("Header", "+", "include/menu.h", "", ""))
            writer.writerow(
                ("Function", "+", "menu_set_header", "void", "Menu*, const char*")
            )
            writer.writerow(("Variable", "-", "menu_generation", "const uint32_t", ""))
        self.lock = self.root / "firmware-api.lock.csv"

    def close(self) -> None:
        self.temporary.cleanup()

    def run(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                os.fspath(SNAPSHOTTER),
                "--root",
                os.fspath(self.root),
                "--api",
                os.fspath(self.api),
                "--output",
                os.fspath(self.lock),
                *arguments,
            ],
            capture_output=True,
            check=False,
            text=True,
        )


class SnapshotFirmwareApiTests(unittest.TestCase):
    def setUp(self) -> None:
        self.fixture = FirmwareApiFixture()

    def tearDown(self) -> None:
        self.fixture.close()

    def test_writes_deterministic_versioned_symbol_snapshot(self) -> None:
        result = self.fixture.run()

        self.assertEqual(result.returncode, 0, result.stderr)
        with self.fixture.lock.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual(
            rows,
            [
                {
                    "api_version": "88.3",
                    "kind": "Function",
                    "status": "+",
                    "name": "menu_set_header",
                    "name_hash": "0xd4d88cad",
                    "signature": "void menu_set_header(Menu*, const char*)",
                    "source_owner": "include/menu.h",
                    "compatibility": "supported",
                },
                {
                    "api_version": "88.3",
                    "kind": "Variable",
                    "status": "-",
                    "name": "menu_generation",
                    "name_hash": "0xffc1e465",
                    "signature": "const uint32_t menu_generation",
                    "source_owner": "include/menu.h",
                    "compatibility": "disabled",
                },
            ],
        )

    def test_check_accepts_exact_snapshot_and_rejects_stale_snapshot(self) -> None:
        self.assertEqual(self.fixture.run().returncode, 0)
        exact = self.fixture.run("--check")
        self.assertEqual(exact.returncode, 0, exact.stderr)

        self.fixture.lock.write_text("stale\n", encoding="utf-8")
        stale = self.fixture.run("--check")
        self.assertNotEqual(stale.returncode, 0)
        self.assertIn("firmware API snapshot is stale", stale.stderr)

    def test_rejects_pending_api_entries(self) -> None:
        with self.fixture.api.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.reader(stream))
        rows[2][1] = "?"
        with self.fixture.api.open("w", encoding="utf-8", newline="") as stream:
            csv.writer(stream, lineterminator="\n").writerows(rows)

        result = self.fixture.run()

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("pending API entry", result.stderr)

    def test_rejects_a_symbol_without_a_source_owner(self) -> None:
        with self.fixture.api.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.reader(stream))
        rows[3][2] = "unowned_function"
        with self.fixture.api.open("w", encoding="utf-8", newline="") as stream:
            csv.writer(stream, lineterminator="\n").writerows(rows)

        result = self.fixture.run()

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("source owner not found: unowned_function", result.stderr)

    def test_rejects_name_hash_collisions(self) -> None:
        with self.fixture.api.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.reader(stream))
        rows.append(("Variable", "+", "menu_set_header", "uint32_t", ""))
        with self.fixture.api.open("w", encoding="utf-8", newline="") as stream:
            csv.writer(stream, lineterminator="\n").writerows(rows)

        result = self.fixture.run()

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("duplicate API symbol: menu_set_header", result.stderr)

    def test_accepts_non_utf8_bytes_outside_api_identifiers(self) -> None:
        header = self.fixture.root / "include" / "menu.h"
        header.write_bytes(header.read_bytes() + b"/* copyright \xa9 */\n")

        result = self.fixture.run()

        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
