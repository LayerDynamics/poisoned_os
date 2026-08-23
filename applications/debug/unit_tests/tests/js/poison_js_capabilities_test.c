#include "../test.h"
#include "../../../../system/js_app/js_capabilities.h"
#include "../../../../system/js_app/js_modules.h"

MU_TEST(poison_js_capability_map_is_explicit_and_default_deny) {
    mu_check(js_capability_module_known("storage"));
    mu_check(js_capability_for_module("storage") == JsCapabilityStorage);
    mu_check(js_capability_module_allowed("storage", JsCapabilityStorage));
    mu_check(!js_capability_module_allowed("storage", JsCapabilitySerial));
    mu_check(!js_capability_module_known("native_ffi"));
    mu_check(!js_capability_module_allowed("native_ffi", UINT32_MAX));
    mu_check(js_capability_module_allowed("evidence", JsCapabilityEvidence));
}

MU_TEST(poison_js_managed_paths_are_revision_scoped) {
    const char* script =
        "/scripts/javascript/demo/versions/"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef/src/main.js";
    char path[JS_PROJECT_PATH_MAX];
    mu_check(js_modules_test_resolve_project_path(script, "data/report.json", path));
    mu_check(
        strcmp(
            path,
            "/scripts/javascript/demo/versions/"
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef/"
            "data/report.json") == 0);
    mu_check(!js_modules_test_resolve_project_path(script, "../other/project.json", path));
    mu_check(!js_modules_test_resolve_project_path(script, "data/../../secret", path));
    mu_check(!js_modules_test_resolve_project_path(script, "data//secret", path));
    mu_check(!js_modules_test_resolve_project_path(
        "/scripts/javascript/demo/versions/not-a-digest/src/main.js", "data/file", path));
}

MU_TEST(poison_js_source_modules_normalize_within_revision_root) {
    const char* script =
        "/scripts/javascript/demo/versions/"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef/src/main.js";
    char path[JS_PROJECT_PATH_MAX];
    mu_check(js_modules_test_resolve_source_path(
        script, "src", "../vendor/tiny-value/1.0.0/index.js", path));
    mu_check(
        strcmp(
            path,
            "/scripts/javascript/demo/versions/"
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef/"
            "vendor/tiny-value/1.0.0/index.js") == 0);
    mu_check(!js_modules_test_resolve_source_path(script, "src", "../../../../secret.js", path));
    mu_check(!js_modules_test_resolve_source_path(script, "src", "/ext/secret.js", path));
    mu_check(!js_modules_test_resolve_source_path(script, "src", "bad\\module.js", path));
}

MU_TEST(poison_js_bare_modules_resolve_only_from_declared_lock_main) {
    const char* script =
        "/scripts/javascript/demo/versions/"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef/src/main.js";
    const char* lock = "{\"schema\":\"poison.javascript.lock/v1\",\"runtime\":\"poison-mjs-1\","
                       "\"entrypoint\":\"src/main.js\",\"dependencies\":[{\"name\":\"tiny-value\","
                       "\"version\":\"1.0.0\",\"main\":\"index.js\",\"integrity\":\"sha256-"
                       "aNMrtBY2W3J/Mj4s1dDNAaebWPMvS51WJIqKg0ZFNgk=\",\"source\":\"bundled\","
                       "\"license\":\"MIT\",\"runtime\":\"poison-mjs-1\",\"dependencies\":[],"
                       "\"files\":[{\"path\":\"index.js\",\"sha256\":"
                       "\"22bbdaab621a68527a72cb13119a5f8a93efe22a05939b5d48aba6a1835e6f5e\","
                       "\"bytes\":20}]}]}";
    char path[JS_PROJECT_PATH_MAX];
    mu_check(js_modules_test_resolve_bare_source_path(script, lock, "tiny-value", path));
    mu_check(strstr(path, "/vendor/tiny-value/1.0.0/index.js") != NULL);
    mu_check(!js_modules_test_resolve_bare_source_path(script, lock, "missing", path));
}

MU_TEST(poison_js_supported_builtins_resolve_to_immutable_project_shims) {
    const char* script =
        "/scripts/javascript/demo/versions/"
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef/src/main.js";
    char path[JS_PROJECT_PATH_MAX];
    mu_check(js_modules_test_resolve_builtin_source_path(script, "path", path));
    mu_check(strstr(path, "/_poison/node/path.js") != NULL);
    mu_check(js_modules_test_resolve_builtin_source_path(script, "node:path", path));
    mu_check(!js_modules_test_resolve_builtin_source_path(script, "child_process", path));
}

MU_TEST_SUITE(poison_js_capabilities_suite) {
    MU_RUN_TEST(poison_js_capability_map_is_explicit_and_default_deny);
    MU_RUN_TEST(poison_js_managed_paths_are_revision_scoped);
    MU_RUN_TEST(poison_js_source_modules_normalize_within_revision_root);
    MU_RUN_TEST(poison_js_bare_modules_resolve_only_from_declared_lock_main);
    MU_RUN_TEST(poison_js_supported_builtins_resolve_to_immutable_project_shims);
}

void poison_js_capabilities_run_tests(void) {
    MU_RUN_SUITE(poison_js_capabilities_suite);
}
