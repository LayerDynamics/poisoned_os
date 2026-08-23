from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class JavascriptRuntimeBuildTests(unittest.TestCase):
    def test_runner_ui_is_external_but_core_runtime_and_browser_adapter_remain_internal(
        self,
    ) -> None:
        manifest = (
            ROOT / "applications/system/js_app/application.fam"
        ).read_text(encoding="utf-8")
        source = (ROOT / "applications/system/js_app/js_app.c").read_text(
            encoding="utf-8"
        )
        workload_manifest = (
            ROOT / "applications/services/poison_workload/application.fam"
        ).read_text(encoding="utf-8")
        workload_adapter = (
            ROOT
            / "applications"
            / "services"
            / "poison_workload"
            / "poison_workload_js_adapter.c"
        ).read_text(encoding="utf-8")
        system_package = (
            ROOT / "applications/system/application.fam"
        ).read_text(encoding="utf-8")

        core = manifest[: manifest.index('appid="js_runner"')]
        runner = manifest[manifest.index('appid="js_runner"') :]
        self.assertIn('appid="js_app"', core)
        self.assertIn("apptype=FlipperAppType.SYSTEM", core)
        self.assertNotIn("cdefines=", runner.split(")", 1)[0])
        self.assertIn("apptype=FlipperAppType.EXTERNAL", runner)
        self.assertIn('fap_category="Scripts"', runner)
        self.assertIn('fap_icon_assets="../../../assets/icons/Archive"', runner)
        self.assertIn('requires=["js_app"]', runner)
        self.assertIn('"js_runner"', system_package)
        self.assertNotIn("JS_RUNNER_FAP", source)
        self.assertIn('"js_runner.c"', runner)
        self.assertIn("loader_enqueue_launch", source)
        self.assertIn('EXT_PATH("apps/Scripts/js_runner.fap")', source)
        self.assertIn('requires=["js_app"', workload_manifest)
        self.assertIn("js_thread_run_managed", workload_adapter)

    def test_crypto_plugin_consumes_firmware_service_api(self) -> None:
        manifest = (
            ROOT / "applications/system/js_app/application.fam"
        ).read_text(encoding="utf-8")
        source = (
            ROOT / "applications/system/js_app/modules/js_crypto.c"
        ).read_text(encoding="utf-8")
        rpc_manifest = (
            ROOT / "applications/services/rpc/application.fam"
        ).read_text(encoding="utf-8")
        crypto_header = (
            ROOT / "applications/services/rpc/rpc_poison_crypto.h"
        ).read_text(encoding="utf-8")
        mbedtls_build = (ROOT / "lib/mbedtls.scons").read_text(encoding="utf-8")

        crypto_app = manifest[manifest.index('appid="js_crypto"') :]
        self.assertIn('requires=["js_app", "rpc_start"]', crypto_app)
        self.assertIn('sources=["modules/js_crypto.c"]', crypto_app)
        self.assertNotIn("rpc_poison_crypto.c", crypto_app)
        self.assertIn("#include <rpc/rpc_poison_crypto.h>", source)
        self.assertIn('"rpc_poison_crypto.h"', rpc_manifest)

        self.assertNotIn("MJS_ERROR", source)
        self.assertIn("MJS_INTERNAL_ERROR", source)
        self.assertIn("char* bytes = mjs_array_buf_get_ptr", source)
        self.assertIn("*data = (const uint8_t*)bytes", source)
        self.assertIn("mjs_mk_array_buf(mjs, (char*)bytes", source)
        self.assertIn("mjs_mk_array_buf(mjs, (char*)digest", source)
        self.assertIn("mjs_mk_array_buf(mjs, (char*)output", source)
        self.assertNotIn("#include <mbedtls/", source)
        self.assertIn("poison_crypto_sha256", source)
        self.assertIn("poison_crypto_hmac_sha256", source)
        self.assertIn("poison_crypto_sha256", crypto_header)
        self.assertIn("poison_crypto_hmac_sha256", crypto_header)
        self.assertIn('File("mbedtls/library/constant_time.c")', mbedtls_build)


if __name__ == "__main__":
    unittest.main()
