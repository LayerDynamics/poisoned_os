from __future__ import annotations

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import tempfile
import types
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
TOOLCHAIN_VERIFIER = REPOSITORY_ROOT / "tools" / "verify_toolchain.py"
REPRODUCIBLE_BUILD_CHECKER = REPOSITORY_ROOT / "tools" / "check_reproducible_build.py"
RECURSIVE_GLOB = REPOSITORY_ROOT / "scripts" / "fbt_tools" / "sconsrecursiveglob.py"
FBT_DIST = REPOSITORY_ROOT / "scripts" / "fbt_tools" / "fbt_dist.py"
FW_SIZE_SCRIPT = REPOSITORY_ROOT / "scripts" / "fwsize.py"
TOOLCHAIN_SITE_PACKAGES = (
    REPOSITORY_ROOT / "toolchain" / "arm64-darwin" / "lib" / "python3.11" / "site-packages"
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def host_id() -> str:
    system = "darwin" if sys.platform == "darwin" else sys.platform
    return f"{platform.machine()}-{system}"


def load_recursive_glob_module():
    scons = types.ModuleType("SCons")
    scons.Node = types.SimpleNamespace(FS=types.SimpleNamespace(Dir=object))
    scons_node = types.ModuleType("SCons.Node")
    scons_node_fs = types.ModuleType("SCons.Node.FS")
    scons_node_fs.has_glob_magic = lambda pattern: "*" in pattern
    scons_script = types.ModuleType("SCons.Script")
    scons_script.Flatten = lambda values: list(values)
    fbt = types.ModuleType("fbt")
    fbt_util = types.ModuleType("fbt.util")
    fbt_util.GLOB_FILE_EXCLUSION = []
    modules = {
        "SCons": scons,
        "SCons.Node": scons_node,
        "SCons.Node.FS": scons_node_fs,
        "SCons.Script": scons_script,
        "fbt": fbt,
        "fbt.util": fbt_util,
    }
    spec = importlib.util.spec_from_file_location("recursive_glob_under_test", RECURSIVE_GLOB)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    from unittest import mock

    with mock.patch.dict(sys.modules, modules):
        spec.loader.exec_module(module)
    return module


def load_fbt_dist_module():
    scons = types.ModuleType("SCons")
    scons_action = types.ModuleType("SCons.Action")
    scons_action.Action = object
    scons_builder = types.ModuleType("SCons.Builder")
    scons_builder.Builder = object
    scons_defaults = types.ModuleType("SCons.Defaults")
    scons_defaults.Touch = object
    modules = {
        "SCons": scons,
        "SCons.Action": scons_action,
        "SCons.Builder": scons_builder,
        "SCons.Defaults": scons_defaults,
    }
    spec = importlib.util.spec_from_file_location("fbt_dist_under_test", FBT_DIST)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    from unittest import mock

    with mock.patch.dict(sys.modules, modules):
        spec.loader.exec_module(module)
    return module


def load_fwsize_module():
    ansi = types.ModuleType("ansi")
    ansi_color = types.ModuleType("ansi.color")
    ansi_color.fg = types.SimpleNamespace(yellow=lambda value: value)
    flipper = types.ModuleType("flipper")
    flipper_app = types.ModuleType("flipper.app")
    flipper_app.App = object
    modules = {
        "ansi": ansi,
        "ansi.color": ansi_color,
        "flipper": flipper,
        "flipper.app": flipper_app,
    }
    spec = importlib.util.spec_from_file_location("fwsize_under_test", FW_SIZE_SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    from unittest import mock

    with mock.patch.dict(sys.modules, modules):
        spec.loader.exec_module(module)
    return module


class ToolchainFixture:
    def __init__(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.toolchain = self.root / "toolchain" / host_id()
        (self.toolchain / "bin").mkdir(parents=True)
        (self.toolchain / "VERSION").write_text("39\n", encoding="utf-8")
        self.compiler = self.toolchain / "bin" / "compiler"
        self.compiler.write_text("#!/bin/sh\nprintf 'compiler 1.0\\n'\n", encoding="utf-8")
        self.compiler.chmod(0o755)
        self.archive = self.root / "toolchain" / "bundle.tar.gz"
        self.archive.write_bytes(b"fixture archive\n")
        self.flags = self.root / "compiler-flags.scons"
        self.flags.write_text("CCFLAGS = ['-Werror']\n", encoding="utf-8")
        self.manifest_path = self.root / "manifest.lock.json"
        self.manifest = {
            "schema": "poison.toolchain/v1",
            "bundle": {
                "version": "39",
                "host": host_id(),
                "path": f"toolchain/{host_id()}",
                "archivePath": "toolchain/bundle.tar.gz",
                "archiveUrl": "https://example.invalid/bundle.tar.gz",
                "archiveSha256": sha256(self.archive),
            },
            "tools": [
                {
                    "name": "compiler",
                    "path": "bin/compiler",
                    "sha256": sha256(self.compiler),
                    "versionCommand": ["bin/compiler"],
                    "versionPattern": "compiler 1.0",
                }
            ],
            "buildInputs": [
                {"path": "compiler-flags.scons", "sha256": sha256(self.flags)}
            ],
        }
        self.write_manifest()

    def write_manifest(self) -> None:
        self.manifest_path.write_text(
            json.dumps(self.manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    def run(
        self,
        extra_env: dict[str, str] | None = None,
        *arguments: str,
    ) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment.update(extra_env or {})
        return subprocess.run(
            [
                sys.executable,
                os.fspath(TOOLCHAIN_VERIFIER),
                "--root",
                os.fspath(self.root),
                "--manifest",
                os.fspath(self.manifest_path),
                *arguments,
            ],
            capture_output=True,
            check=False,
            env=environment,
            text=True,
        )

    def close(self) -> None:
        self.temporary.cleanup()


class VerifyToolchainTests(unittest.TestCase):
    def setUp(self) -> None:
        self.fixture = ToolchainFixture()

    def tearDown(self) -> None:
        self.fixture.close()

    def test_accepts_exact_host_bundle_tools_and_build_inputs(self) -> None:
        result = self.fixture.run()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("toolchain verification passed", result.stdout)

    def test_rejects_wrong_bundle_version(self) -> None:
        (self.fixture.toolchain / "VERSION").write_text("38\n", encoding="utf-8")
        result = self.fixture.run()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("bundle version mismatch", result.stderr)

    def test_rejects_wrong_archive_digest(self) -> None:
        self.fixture.archive.write_bytes(b"tampered archive\n")
        result = self.fixture.run()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("archive digest mismatch", result.stderr)

    def test_rejects_missing_tool(self) -> None:
        self.fixture.compiler.unlink()
        result = self.fixture.run()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing tool: compiler", result.stderr)

    def test_rejects_unmanaged_host_fallback(self) -> None:
        result = self.fixture.run({"FBT_NOENV": "1"})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FBT_NOENV host fallback is forbidden", result.stderr)

    def test_rejects_altered_compiler_flags(self) -> None:
        self.fixture.flags.write_text("CCFLAGS = ['-w']\n", encoding="utf-8")
        result = self.fixture.run()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("build input digest mismatch: compiler-flags.scons", result.stderr)

    def test_write_refreshes_only_declared_build_input_digests(self) -> None:
        self.fixture.flags.write_text("CCFLAGS = ['-Werror', '-Wextra']\n", encoding="utf-8")
        original = json.loads(self.fixture.manifest_path.read_text(encoding="utf-8"))

        refreshed = self.fixture.run(None, "--write")

        self.assertEqual(refreshed.returncode, 0, refreshed.stderr)
        self.assertIn("toolchain build-input lock refreshed", refreshed.stdout)
        updated = json.loads(self.fixture.manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(updated["bundle"], original["bundle"])
        self.assertEqual(updated["tools"], original["tools"])
        self.assertEqual(updated["buildInputs"][0]["path"], "compiler-flags.scons")
        self.assertEqual(updated["buildInputs"][0]["sha256"], sha256(self.fixture.flags))
        checked = self.fixture.run()
        self.assertEqual(checked.returncode, 0, checked.stderr)

    def test_write_rejects_a_missing_declared_input_without_changing_lock(self) -> None:
        before = self.fixture.manifest_path.read_bytes()
        self.fixture.flags.unlink()

        result = self.fixture.run(None, "--write")

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing build input: compiler-flags.scons", result.stderr)
        self.assertEqual(self.fixture.manifest_path.read_bytes(), before)


class ReproducibleBuildTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.left = self.root / "left"
        self.right = self.root / "right"
        for output in (self.left, self.right):
            files = {
                "build/f7-firmware-D/firmware.bin": b"firmware\n",
                "build/f7-firmware-D/firmware.map": b"map\n",
                "build/f7-firmware-D/resources/Manifest": b"resources\n",
                "build/f7-firmware-D/assets/compiled/firmware_api_table.h": b"api\n",
                "build/f7-updater-D/updater.bin": b"updater\n",
            }
            for relative, contents in files.items():
                path = output / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(contents)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def run_checker(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                os.fspath(REPRODUCIBLE_BUILD_CHECKER),
                "--left",
                os.fspath(self.left),
                "--right",
                os.fspath(self.right),
            ],
            capture_output=True,
            check=False,
            text=True,
        )

    def test_compares_required_artifact_families_and_detects_changes(self) -> None:
        left_debug_map = self.left / "build/f7-firmware-D/.extapps/example.elf.map"
        right_debug_map = self.right / "build/f7-firmware-D/.extapps/example.elf.map"
        left_debug_map.parent.mkdir(parents=True)
        right_debug_map.parent.mkdir(parents=True)
        left_debug_map.write_bytes(b"left checkout path\n")
        right_debug_map.write_bytes(b"right checkout path\n")

        matching = self.run_checker()
        self.assertEqual(matching.returncode, 0, matching.stderr)
        self.assertIn("reproducible build comparison passed", matching.stdout)

        changed = self.right / "build/f7-firmware-D/resources/Manifest"
        changed.write_bytes(b"changed\n")
        mismatch = self.run_checker()
        self.assertNotEqual(mismatch.returncode, 0)
        self.assertIn("artifact content mismatch", mismatch.stderr)
        self.assertIn(
            "first differing line 1: left='resources', right='changed'",
            mismatch.stderr,
        )

    def test_compiler_maps_checkout_paths_out_of_payloads(self) -> None:
        compiler_configuration = (
            REPOSITORY_ROOT / "site_scons" / "cc.scons"
        ).read_text(encoding="utf-8")

        self.assertIn(
            '"-ffile-prefix-map=${ROOT_DIR.abspath}=.",', compiler_configuration
        )

    def test_source_collection_is_sorted_for_deterministic_link_order(self) -> None:
        module = load_recursive_glob_module()

        class Source:
            def __init__(self, path: str) -> None:
                self.path = path

        class Environment:
            def GlobRecursive(self, pattern, node, exclude):
                del node, exclude
                return {
                    "*.c": [Source("z.c"), Source("a.c")],
                    "*.cpp": [Source("m.cpp")],
                }[pattern]

        sources = module.GatherSources(Environment(), ["*.c", "*.cpp"], "fixture")

        self.assertEqual([source.path for source in sources], ["a.c", "m.cpp", "z.c"])


class BuildProfileIsolationTests(unittest.TestCase):
    def test_firmware_build_enforces_radio_boundary_with_one_page_reserve(self) -> None:
        fwsize = load_fwsize_module()
        firmware_graph = (REPOSITORY_ROOT / "firmware.scons").read_text(
            encoding="utf-8"
        )

        boundary = 0x080D7000
        origin = 0x08000000
        self.assertEqual(
            fwsize.radio_gap_bytes(boundary - origin - 4096, origin, boundary),
            4096,
        )
        self.assertTrue(
            fwsize.radio_layout_fits(
                boundary - origin - 4096, origin, boundary, reserve_pages=1
            )
        )
        self.assertFalse(
            fwsize.radio_layout_fits(
                boundary - origin - 4095, origin, boundary, reserve_pages=1
            )
        )
        self.assertIn('"--radio"', firmware_graph)
        self.assertIn('"--reserve-pages", "1"', firmware_graph)

    def test_default_device_build_is_compact_release_profile(self) -> None:
        options = (REPOSITORY_ROOT / "fbt_options.py").read_text(encoding="utf-8")

        self.assertRegex(options, r"(?m)^COMPACT = 1$")
        self.assertRegex(options, r"(?m)^DEBUG = 0$")

    def test_compact_profile_applies_lto_to_internal_libraries(self) -> None:
        firmware_graph = (REPOSITORY_ROOT / "firmware.scons").read_text(
            encoding="utf-8"
        )

        self.assertIn(
            'compact_firmware_lto = ENV["COMPACT"] and fw_build_meta["type"] == "firmware"',
            firmware_graph,
        )
        self.assertIn(
            'compact_lib_ccflags = ["-Oz", "-flto"] if compact_firmware_lto else None',
            firmware_graph,
        )
        self.assertIn(
            '"AR": "arm-none-eabi-gcc-ar",',
            firmware_graph,
        )
        self.assertIn(
            '"RANLIB": "arm-none-eabi-gcc-ranlib",',
            firmware_graph,
        )
        self.assertIn(
            '"ARCOM": "$AR $ARFLAGS $TARGET $SOURCES",',
            firmware_graph,
        )
        compact_library_flags = r'"CCFLAGS": compact_lib_ccflags\s+or \['
        self.assertRegex(firmware_graph, compact_library_flags)
        self.assertEqual(len(re.findall(compact_library_flags, firmware_graph)), 2)
        self.assertRegex(
            firmware_graph,
            r'"furi": \{\s*(?:#.*\s*)*"CCFLAGS": \[\s*"-Os",',
        )

        firmware_options = (
            REPOSITORY_ROOT / "site_scons" / "firmwareopts.scons"
        ).read_text(encoding="utf-8")
        self.assertIn('if ENV["IS_BASE_FIRMWARE"]:', firmware_options)
        self.assertIn('compact_ccflags.append("-flto")', firmware_options)
        self.assertIn('compact_linkflags.append("-flto")', firmware_options)

    def test_external_apps_keep_relocatable_symbol_metadata(self) -> None:
        external_app_graph = (
            REPOSITORY_ROOT / "site_scons" / "extapps.scons"
        ).read_text(encoding="utf-8")

        self.assertIn(
            'CCFLAGS=[flag for flag in appenv["CCFLAGS"] if flag != "-flto"],',
            external_app_graph,
        )
        self.assertIn(
            'LINKFLAGS=[flag for flag in appenv["LINKFLAGS"] if flag != "-flto"],',
            external_app_graph,
        )

    def test_firmware_api_lookup_table_is_an_optional_sd_resource(self) -> None:
        sdk_builder = (REPOSITORY_ROOT / "scripts" / "fbt_tools" / "fbt_sdk.py").read_text(
            encoding="utf-8"
        )
        firmware_graph = (REPOSITORY_ROOT / "firmware.scons").read_text(encoding="utf-8")
        external_app_graph = (
            REPOSITORY_ROOT / "site_scons" / "extapps.scons"
        ).read_text(encoding="utf-8")
        resolver = (
            REPOSITORY_ROOT
            / "applications"
            / "services"
            / "loader"
            / "firmware_api"
            / "firmware_api.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn('"ApiExternalTable": Builder(', sdk_builder)
        self.assertIn(
            'FW_API_KEEP=firmware_apitable[1]', external_app_graph.replace(" ", "")
        )
        self.assertIn('fwenv["APPENV"].ApiExternalTable(', firmware_graph)
        self.assertIn('"@${FW_API_KEEP.abspath}"', firmware_graph)
        self.assertIn('EXT_PATH("firmware_api.bin")', resolver)
        self.assertIn("crc32_calc_buffer", resolver)


class ExternalFirmwareApiTableTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        sys.path.insert(0, os.fspath(TOOLCHAIN_SITE_PACKAGES))
        from scripts.fbt.sdk import cache

        cls.cache = cache

    def test_table_round_trip_and_corruption_rejection(self) -> None:
        entries = [("second", 0x08002000), ("first", 0x08001000)]
        encoded = self.cache.encode_external_api_table(0x0058003D, entries)

        version, decoded = self.cache.decode_external_api_table(encoded)

        self.assertEqual(version, 0x0058003D)
        self.assertEqual(
            decoded,
            sorted(
                (self.cache.elf_gnu_hash(name), address) for name, address in entries
            ),
        )

        corrupted = bytearray(encoded)
        corrupted[-1] ^= 0x01
        with self.assertRaises(self.cache.ExternalApiTableError):
            self.cache.decode_external_api_table(bytes(corrupted))

    def test_nondefault_app_set_has_distinct_build_and_distribution_flavors(self) -> None:
        module = load_fbt_dist_module()
        default = {
            "TARGET_HW": 7,
            "FIRMWARE_APP_SET": "default",
            "DEBUG": True,
            "COMPACT": False,
        }
        unit_tests = {**default, "FIRMWARE_APP_SET": "unit_tests"}

        self.assertEqual(module.GetProjetDirName(default), "f7-D")
        self.assertEqual(module.GetProjetDirName(default, "firmware"), "f7-firmware-D")
        self.assertEqual(module.GetProjetDirName(unit_tests), "f7-unit_tests-D")
        self.assertEqual(
            module.GetProjetDirName(unit_tests, "firmware"),
            "f7-firmware-unit_tests-D",
        )

    def test_usb_install_markers_are_scoped_to_the_build_profile(self) -> None:
        sconstruct = (REPOSITORY_ROOT / "SConstruct").read_text(encoding="utf-8")

        self.assertIn('f"#build/{dist_dir_name}-usbinstall.flag"', sconstruct)
        self.assertIn('f"#build/{dist_dir_name}-minusbinstall.flag"', sconstruct)


if __name__ == "__main__":
    unittest.main()
