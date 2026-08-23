from __future__ import annotations

import csv
import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "approve_api_symbols.py"


def load_module():
    spec = importlib.util.spec_from_file_location("approve_api_symbols", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {MODULE_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ApproveApiSymbolsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.module = load_module()

    def test_approves_only_pending_rows_and_version(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "api.csv"
            path.write_text(
                "entry,status,name,type,params\n"
                "Version,v,88.4,,\n"
                "Header,+,existing.h,,\n"
                "Function,?,new_api,void,\n",
                encoding="utf-8",
            )
            self.assertEqual(self.module.approve(path), 1)
            with path.open(newline="", encoding="utf-8") as source:
                rows = list(csv.DictReader(source))
        self.assertEqual([row["status"] for row in rows], ["+", "+", "+"])

    def test_rejects_table_without_pending_version(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "api.csv"
            path.write_text(
                "entry,status,name,type,params\nVersion,+,88.4,,\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(self.module.ApiApprovalError, "pending Version"):
                self.module.approve(path)


if __name__ == "__main__":
    unittest.main()
