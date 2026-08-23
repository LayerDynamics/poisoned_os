#include "../test.h"
#include "../../../../system/js_app/js_limits.h"
#include "../../../../../lib/mjs/mjs_core_public.h"
#include "../../../../../lib/mjs/mjs_exec_public.h"
#include "../../../../../lib/mjs/mjs_object_public.h"
#include "../../../../../lib/mjs/mjs_primitive_public.h"
#include "../../../../../lib/mjs/mjs_util_public.h"

void mjs_set_parser_depth_limit(struct mjs* mjs, size_t maximum_depth);
void mjs_set_debug_hook(struct mjs* mjs, mjs_debug_hook_t hook, void* context);
mjs_val_t mjs_debug_get_scope(struct mjs* mjs, size_t depth);
mjs_err_t mjs_debug_eval(
    struct mjs* mjs,
    const char* src,
    mjs_val_t* res,
    char* error,
    size_t error_size);
int mjs_is_closure(mjs_val_t value);

MU_TEST(poison_js_limits_reports_mjs_reserved_memory) {
    struct mjs* mjs = mjs_create(NULL);
    mu_check(mjs != NULL);
    mu_check(mjs_memory_usage(mjs) > 0u);
    mjs_destroy(mjs);
}

MU_TEST(poison_js_limits_fuel_and_reason) {
    JsLimits limits;
    const JsLimitsConfig config = {.fuel_limit = 2};
    js_limits_init(&limits, &config, 10u);
    mu_check(!js_limits_poll(&limits, 10u, 1000u));
    mu_check(!js_limits_poll(&limits, 10u, 1000u));
    mu_check(js_limits_poll(&limits, 10u, 1000u));
    mu_check(js_limits_reason(&limits) == JsLimitReasonFuel);
    mu_check(js_limits_triggered(&limits));
}

MU_TEST(poison_js_limits_wall_time_and_wrap) {
    JsLimits limits;
    const JsLimitsConfig config = {.wall_time_ms = 10};
    js_limits_init(&limits, &config, UINT32_MAX - 2u);
    mu_check(!js_limits_poll(&limits, 1u, 1000u));
    mu_check(js_limits_poll(&limits, 20u, 1000u));
    mu_check(js_limits_reason(&limits) == JsLimitReasonWallTime);
}

MU_TEST(poison_js_limits_accounts_bounded_resources) {
    JsLimits limits;
    const JsLimitsConfig config = {.heap_bytes = 8u, .open_handles = 2u};
    js_limits_init(&limits, &config, 0u);
    const JsLimitsUsage heap = {.heap_bytes = 8u};
    mu_check(js_limits_account(&limits, &heap));
    const JsLimitsUsage handle = {.open_handles = 3u};
    mu_check(!js_limits_account(&limits, &handle));
    mu_check(js_limits_reason(&limits) == JsLimitReasonHandle);
    mu_assert_string_eq(
        "javascript handle limit exceeded", js_limits_reason_text(js_limits_reason(&limits)));
}

MU_TEST(poison_js_parser_depth_limit_fails_closed) {
    struct mjs* mjs = mjs_create(NULL);
    mu_check(mjs != NULL);
    mjs_set_parser_depth_limit(mjs, 2u);
    mu_check(mjs_exec(mjs, "if(true){{{1;}}}", NULL) != MJS_OK);
    mjs_destroy(mjs);
}

MU_TEST(poison_mjs_functions_capture_only_required_scopes) {
    struct mjs* mjs = mjs_create(NULL);
    mu_check(mjs != NULL);

    mjs_val_t plain = MJS_UNDEFINED;
    mu_check(mjs_exec(mjs, "function plain() { return 1; } plain;", &plain) == MJS_OK);
    mu_check(mjs_is_function(plain));
    mu_check(!mjs_is_closure(plain));

    mjs_val_t closure = MJS_UNDEFINED;
    mu_check(
        mjs_exec(
            mjs,
            "var make = function(value) { return function() { return value; }; }; make(7);",
            &closure) == MJS_OK);
    mu_check(mjs_is_function(closure));
    mu_check(mjs_is_closure(closure));

    mjs_val_t result = MJS_UNDEFINED;
    mu_check(mjs_apply(mjs, &result, closure, MJS_UNDEFINED, 0u, NULL) == MJS_OK);
    mu_assert_int_eq(7, mjs_get_int(mjs, result));
    mjs_destroy(mjs);
}

typedef struct {
    size_t line_events;
    size_t statement_events;
    int statement_line;
    bool eval_succeeded;
    bool scope_visible;
    bool location_visible;
    size_t frame_count;
} PoisonMjsDebuggerProbe;

static void poison_mjs_debugger_probe(
    struct mjs* mjs,
    mjs_debug_event_t event,
    const char* filename,
    int line,
    void* context) {
    PoisonMjsDebuggerProbe* probe = context;
    if(event == MJS_DEBUG_EVENT_LINE) {
        ++probe->line_events;
        return;
    }
    ++probe->statement_events;
    probe->statement_line = line;
    probe->location_visible = filename && strcmp(filename, "<stdin>") == 0 &&
                              mjs_get_offset_by_call_frame_num(mjs, 0) >= 0;

    for(size_t depth = 0u;; ++depth) {
        const mjs_val_t scope = mjs_debug_get_scope(mjs, depth);
        if(scope == MJS_UNDEFINED) break;
        if(mjs_is_object(scope) && mjs_get_int(mjs, mjs_get(mjs, scope, "value", ~0)) == 41) {
            probe->scope_visible = true;
            break;
        }
    }
    while(mjs_get_offset_by_call_frame_num(mjs, (int)probe->frame_count) >= 0)
        ++probe->frame_count;

    mjs_val_t evaluated = MJS_UNDEFINED;
    char error[64u];
    probe->eval_succeeded = mjs_debug_eval(mjs, "value + 1", &evaluated, error, sizeof(error)) ==
                                MJS_OK &&
                            mjs_get_int(mjs, evaluated) == 42;
}

MU_TEST(poison_mjs_debugger_pauses_inspects_and_resumes) {
    struct mjs* mjs = mjs_create(NULL);
    mu_check(mjs != NULL);
    PoisonMjsDebuggerProbe probe = {0};
    mjs_set_debug_hook(mjs, poison_mjs_debugger_probe, &probe);

    mjs_val_t result = MJS_UNDEFINED;
    mu_check(
        mjs_exec(
            mjs,
            "var value = 41;\nfunction inspect() {\ndebugger;\nreturn value + 1;\n}\n"
            "inspect();",
            &result) == MJS_OK);
    mu_assert_int_eq(42, mjs_get_int(mjs, result));
    mu_check(probe.line_events >= 3u);
    mu_assert_int_eq(1, probe.statement_events);
    mu_assert_int_eq(3, probe.statement_line);
    mu_check(probe.eval_succeeded);
    mu_check(probe.scope_visible);
    mu_check(probe.location_visible);
    mu_check(probe.frame_count >= 2u);

    mjs_set_debug_hook(mjs, NULL, NULL);
    mjs_destroy(mjs);
}

MU_TEST_SUITE(poison_js_limits_suite) {
    MU_RUN_TEST(poison_js_limits_reports_mjs_reserved_memory);
    MU_RUN_TEST(poison_js_limits_fuel_and_reason);
    MU_RUN_TEST(poison_js_limits_wall_time_and_wrap);
    MU_RUN_TEST(poison_js_limits_accounts_bounded_resources);
    MU_RUN_TEST(poison_js_parser_depth_limit_fails_closed);
    MU_RUN_TEST(poison_mjs_functions_capture_only_required_scopes);
    MU_RUN_TEST(poison_mjs_debugger_pauses_inspects_and_resumes);
}

void poison_js_limits_run_tests(void) {
    MU_RUN_SUITE(poison_js_limits_suite);
}
