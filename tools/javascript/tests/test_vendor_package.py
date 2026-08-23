import importlib.util
import base64
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location("vendor_package", ROOT / "tools/javascript/vendor_package.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class VendorPackageTests(unittest.TestCase):
    @staticmethod
    def dependency(name="tiny-value", version="1.0.0", content=b"module.exports = 7;\n", requires=None):
        path = "index.js"
        canonical = path.encode("utf-8") + b"\0" + content
        return {
            "name": name,
            "version": version,
            "main": path,
            "integrity": "sha256-" + base64.b64encode(hashlib.sha256(canonical).digest()).decode("ascii"),
            "source": "bundled",
            "license": "MIT",
            "runtime": MODULE.RUNTIME,
            "dependencies": requires or [],
            "files": [{"path": path, "sha256": hashlib.sha256(content).hexdigest(), "bytes": len(content)}],
        }

    def make_project(self, source: str, **lock_overrides):
        temp = tempfile.TemporaryDirectory()
        project = Path(temp.name)
        (project / "src").mkdir()
        (project / "dist").mkdir()
        (project / "src/index.js").write_text(source, encoding="utf-8")
        (project / "dist/index.js").write_text(source, encoding="utf-8")
        lock = {"schema": MODULE.SCHEMA, "runtime": MODULE.RUNTIME, "entrypoint": "src/index.js", "dependencies": []}
        lock.update(lock_overrides)
        lock_path = project / "javascript.lock.json"
        lock_path.write_text(json.dumps(lock), encoding="utf-8")
        return temp, project, lock_path

    def test_accepts_pure_javascript(self):
        temp, project, lock = self.make_project("const value = 1;\n")
        self.addCleanup(temp.cleanup)
        self.assertEqual(MODULE.scan_sources(project, MODULE.load_lock(lock)), ["src/index.js"])

    def test_rejects_unimplemented_node_builtin(self):
        temp, project, lock = self.make_project("import childProcess from 'node:child_process';\n")
        self.addCleanup(temp.cleanup)
        with self.assertRaisesRegex(MODULE.VendorError, "Node builtin"):
            MODULE.scan_sources(project, MODULE.load_lock(lock))

    def test_accepts_supported_pure_builtin(self):
        temp, project, lock = self.make_project("import path from 'node:path';\npath.join('a', 'b');\n")
        self.addCleanup(temp.cleanup)
        self.assertEqual(MODULE.scan_sources(project, MODULE.load_lock(lock)), ["src/index.js"])

    def test_accepts_storage_backed_fs_builtin(self):
        temp, project, lock = self.make_project("import fs from 'node:fs';\nfs.existsSync('/ext');\n")
        self.addCleanup(temp.cleanup)
        self.assertEqual(MODULE.scan_sources(project, MODULE.load_lock(lock)), ["src/index.js"])

    def test_accepts_extended_runtime_builtins(self):
        temp, project, lock = self.make_project("import os from 'node:os'; import url from 'node:url'; import util from 'node:util';\nos.platform(); url.parse('flipper://device'); util.format('%s', 'ok');\n")
        self.addCleanup(temp.cleanup)
        self.assertEqual(MODULE.scan_sources(project, MODULE.load_lock(lock)), ["src/index.js"])

    def test_accepts_event_and_data_builtins(self):
        temp, project, lock = self.make_project("import timers from 'node:timers'; import stream from 'node:stream'; import qs from 'node:querystring';\nqs.stringify({a: 1}); timers.clearTimeout(null); new stream.Readable();\n")
        self.addCleanup(temp.cleanup)
        self.assertEqual(MODULE.scan_sources(project, MODULE.load_lock(lock)), ["src/index.js"])

    def test_accepts_serial_backed_network_builtins(self):
        temp, project, lock = self.make_project("import http from 'node:http'; import net from 'node:net'; import tls from 'node:tls';\nhttp.get('http://esp.local'); net.connect(443, 'esp.local'); tls.connect({port: 443});\n")
        self.addCleanup(temp.cleanup)
        self.assertEqual(MODULE.scan_sources(project, MODULE.load_lock(lock)), ["src/index.js"])

    def test_requires_sdk_allowlist(self):
        temp, project, lock = self.make_project("import x from '@flipperdevices/fz-sdk/flipper';\n")
        self.addCleanup(temp.cleanup)
        with self.assertRaisesRegex(MODULE.VendorError, "unapproved SDK"):
            MODULE.scan_sources(project, MODULE.load_lock(lock))

    def test_rejects_duplicate_dependencies(self):
        dep = self.dependency()
        temp, project, lock = self.make_project("", dependencies=[dep, dep])
        self.addCleanup(temp.cleanup)
        with self.assertRaisesRegex(MODULE.VendorError, "unique"):
            MODULE.load_lock(lock)

    def test_requires_license_runtime_graph_and_file_identity(self):
        dependency = self.dependency()
        for field in ("main", "license", "runtime", "dependencies", "files"):
            invalid = dict(dependency)
            del invalid[field]
            temp, _project, lock = self.make_project("", dependencies=[invalid])
            self.addCleanup(temp.cleanup)
            with self.assertRaisesRegex(MODULE.VendorError, "dependency"):
                MODULE.load_lock(lock)

    def test_rejects_unknown_top_level_lock_fields(self):
        temp, _project, lock = self.make_project("", unexpected=True)
        self.addCleanup(temp.cleanup)
        with self.assertRaisesRegex(MODULE.VendorError, "fields"):
            MODULE.load_lock(lock)

    def test_rejects_dependency_identity_that_redirects_the_vendor_root(self):
        dependency = self.dependency(name="..")
        temp, _project, lock = self.make_project("", dependencies=[dependency])
        self.addCleanup(temp.cleanup)
        with self.assertRaisesRegex(MODULE.VendorError, "safe package names"):
            MODULE.load_lock(lock)

    def test_rejects_dependency_main_outside_its_inventory(self):
        dependency = self.dependency()
        dependency["main"] = "missing.js"
        temp, _project, lock = self.make_project("", dependencies=[dependency])
        self.addCleanup(temp.cleanup)
        with self.assertRaisesRegex(MODULE.VendorError, "main"):
            MODULE.load_lock(lock)

    def test_rejects_unknown_and_cyclic_dependency_edges(self):
        unknown = self.dependency(requires=["missing"])
        temp, _project, lock = self.make_project("", dependencies=[unknown])
        self.addCleanup(temp.cleanup)
        with self.assertRaisesRegex(MODULE.VendorError, "unknown dependency"):
            MODULE.load_lock(lock)

        first = self.dependency(name="first", requires=["second"])
        second = self.dependency(name="second", requires=["first"])
        temp, _project, lock = self.make_project("", dependencies=[first, second])
        self.addCleanup(temp.cleanup)
        with self.assertRaisesRegex(MODULE.VendorError, "cycle"):
            MODULE.load_lock(lock)

    def test_verifies_every_vendored_file_and_package_integrity(self):
        content = b"module.exports = 7;\n"
        dependency = self.dependency(content=content)
        temp, project, lock_path = self.make_project(
            "const value = require('../vendor/tiny-value/1.0.0/index.js');\n",
            dependencies=[dependency],
        )
        self.addCleanup(temp.cleanup)
        vendor_file = project / "vendor/tiny-value/1.0.0/index.js"
        vendor_file.parent.mkdir(parents=True)
        vendor_file.write_bytes(content)
        lock = MODULE.load_lock(lock_path)
        self.assertEqual(MODULE.verify_vendored_dependencies(project, lock), ["vendor/tiny-value/1.0.0/index.js"])
        vendor_file.write_bytes(b"module.exports = 8;\n")
        with self.assertRaisesRegex(MODULE.VendorError, "digest mismatch"):
            MODULE.verify_vendored_dependencies(project, lock)
