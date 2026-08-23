#include <common/cs_dbg.h>
#include <toolbox/path.h>
#include <toolbox/stream/file_stream.h>
#include <toolbox/strint.h>
#include <loader/firmware_api/firmware_api.h>
#include <flipper_application/api_hashtable/api_hashtable.h>
#include <flipper_application/plugins/composite_resolver.h>
#include <furi_hal.h>
#include <mjs_core.h>
#include <mjs_exec.h>
#include "plugin_api/app_api_interface.h"
#include "js_thread.h"
#include "js_thread_i.h"
#include "js_modules.h"
#include "js_limits.h"
#include <toolbox/pipe.h>
#include <toolbox/cli/cli_ansi.h>
#include <limits.h>

#define TAG "JS"

struct JsThread {
    FuriThread* thread;
    FuriString* path;
    CompositeApiResolver* resolver;
    JsThreadCallback app_callback;
    void* context;
    JsModules* modules;
    JsLimits limits;
    uint32_t accounted_heap_bytes;
    uint16_t accounted_stack_depth;
    uint16_t accounted_parser_depth;
    uint16_t async_handles;
    uint32_t granted_capabilities;
    bool enforce_capabilities;
    bool allow_native_ffi;
    PipeSide* interactive_pipe;
    bool interactive_interrupted;
    bool interactive_debug;
    bool debug_break_requested;
};

static void js_str_print(FuriString* msg_str, struct mjs* mjs) {
    size_t num_args = mjs_nargs(mjs);
    for(size_t i = 0; i < num_args; i++) {
        char* name = NULL;
        size_t name_len = 0;
        int need_free = 0;
        mjs_val_t arg = mjs_arg(mjs, i);
        mjs_err_t err = mjs_to_string(mjs, &arg, &name, &name_len, &need_free);
        if(err != MJS_OK) {
            furi_string_cat_printf(msg_str, "err %s ", mjs_strerror(mjs, err));
        } else {
            furi_string_cat_printf(msg_str, "%s ", name);
        }
        if(need_free) {
            free(name);
            name = NULL;
        }
    }
}

static void js_print(struct mjs* mjs) {
    FuriString* msg_str = furi_string_alloc();
    js_str_print(msg_str, mjs);
    const size_t length = furi_string_size(msg_str);
    const uint32_t bytes = length > UINT32_MAX ? UINT32_MAX : (uint32_t)length;
    if(!js_thread_account(mjs, &(JsLimitsUsage){.logs = bytes})) {
        furi_string_free(msg_str);
        return;
    }

    JsThread* worker = mjs_get_context(mjs);
    furi_assert(worker);
    if(worker->app_callback) {
        worker->app_callback(JsThreadEventPrint, furi_string_get_cstr(msg_str), worker->context);
    } else {
        FURI_LOG_D(TAG, "%s\r\n", furi_string_get_cstr(msg_str));
    }

    furi_string_free(msg_str);

    mjs_return(mjs, MJS_UNDEFINED);
}

static void js_console_log(struct mjs* mjs) {
    FuriString* msg_str = furi_string_alloc();
    js_str_print(msg_str, mjs);
    const size_t length = furi_string_size(msg_str);
    if(!js_thread_account(
           mjs, &(JsLimitsUsage){.logs = length > UINT32_MAX ? UINT32_MAX : (uint32_t)length})) {
        furi_string_free(msg_str);
        return;
    }
    JsThread* worker = mjs_get_context(mjs);
    if(worker && worker->app_callback)
        worker->app_callback(JsThreadEventPrint, furi_string_get_cstr(msg_str), worker->context);
    FURI_LOG_I(TAG, "%s", furi_string_get_cstr(msg_str));
    furi_string_free(msg_str);
    mjs_return(mjs, MJS_UNDEFINED);
}

static void js_console_warn(struct mjs* mjs) {
    FuriString* msg_str = furi_string_alloc();
    js_str_print(msg_str, mjs);
    const size_t length = furi_string_size(msg_str);
    if(!js_thread_account(
           mjs, &(JsLimitsUsage){.logs = length > UINT32_MAX ? UINT32_MAX : (uint32_t)length})) {
        furi_string_free(msg_str);
        return;
    }
    JsThread* worker = mjs_get_context(mjs);
    if(worker && worker->app_callback)
        worker->app_callback(JsThreadEventPrint, furi_string_get_cstr(msg_str), worker->context);
    FURI_LOG_W(TAG, "%s", furi_string_get_cstr(msg_str));
    furi_string_free(msg_str);
    mjs_return(mjs, MJS_UNDEFINED);
}

static void js_console_error(struct mjs* mjs) {
    FuriString* msg_str = furi_string_alloc();
    js_str_print(msg_str, mjs);
    const size_t length = furi_string_size(msg_str);
    if(!js_thread_account(
           mjs, &(JsLimitsUsage){.logs = length > UINT32_MAX ? UINT32_MAX : (uint32_t)length})) {
        furi_string_free(msg_str);
        return;
    }
    JsThread* worker = mjs_get_context(mjs);
    if(worker && worker->app_callback)
        worker->app_callback(JsThreadEventPrint, furi_string_get_cstr(msg_str), worker->context);
    FURI_LOG_E(TAG, "%s", furi_string_get_cstr(msg_str));
    furi_string_free(msg_str);
    mjs_return(mjs, MJS_UNDEFINED);
}

static void js_console_debug(struct mjs* mjs) {
    FuriString* msg_str = furi_string_alloc();
    js_str_print(msg_str, mjs);
    const size_t length = furi_string_size(msg_str);
    if(!js_thread_account(
           mjs, &(JsLimitsUsage){.logs = length > UINT32_MAX ? UINT32_MAX : (uint32_t)length})) {
        furi_string_free(msg_str);
        return;
    }
    JsThread* worker = mjs_get_context(mjs);
    if(worker && worker->app_callback)
        worker->app_callback(JsThreadEventPrint, furi_string_get_cstr(msg_str), worker->context);
    FURI_LOG_D(TAG, "%s", furi_string_get_cstr(msg_str));
    furi_string_free(msg_str);
    mjs_return(mjs, MJS_UNDEFINED);
}

static void js_exit_flag_poll(struct mjs* mjs) {
    JsThread* worker = mjs_get_context(mjs);
    furi_assert(worker);
    size_t footprint = mjs_memory_usage(mjs);
    uint32_t bounded_footprint = footprint > UINT32_MAX ? UINT32_MAX : (uint32_t)footprint;
    if(bounded_footprint > worker->accounted_heap_bytes) {
        JsLimitsUsage increment = {.heap_bytes = bounded_footprint - worker->accounted_heap_bytes};
        if(!js_thread_account(mjs, &increment)) return;
        worker->accounted_heap_bytes = bounded_footprint;
    }
    const size_t call_depth = mjs_call_depth(mjs);
    const uint16_t bounded_depth = call_depth > UINT16_MAX ? UINT16_MAX : (uint16_t)call_depth;
    if(bounded_depth > worker->accounted_stack_depth) {
        const JsLimitsUsage increment = {
            .stack_depth = bounded_depth - worker->accounted_stack_depth,
        };
        if(!js_thread_account(mjs, &increment)) return;
        worker->accounted_stack_depth = bounded_depth;
    }
    const size_t parser_depth = mjs_parser_depth(mjs);
    const uint16_t bounded_parser_depth = parser_depth > UINT16_MAX ? UINT16_MAX :
                                                                      (uint16_t)parser_depth;
    if(bounded_parser_depth > worker->accounted_parser_depth) {
        const JsLimitsUsage increment = {
            .parser_depth = bounded_parser_depth - worker->accounted_parser_depth,
        };
        if(!js_thread_account(mjs, &increment)) return;
        worker->accounted_parser_depth = bounded_parser_depth;
    }
    if(js_limits_poll(&worker->limits, furi_get_tick(), furi_kernel_get_tick_frequency())) {
        mjs_exit(mjs);
        return;
    }
    if(worker->interactive_pipe) {
        if(pipe_state(worker->interactive_pipe) == PipeStateBroken) {
            worker->interactive_interrupted = true;
            mjs_exit(mjs);
            return;
        }
        if(pipe_bytes_available(worker->interactive_pipe)) {
            char input = '\0';
            if(pipe_receive(worker->interactive_pipe, &input, 1u) == 1u && input == CliKeyETX) {
                pipe_send(worker->interactive_pipe, "\r\n", 2u);
                if(worker->interactive_debug) {
                    worker->debug_break_requested = true;
                    return;
                }
                worker->interactive_interrupted = true;
                mjs_exit(mjs);
                return;
            }
        }
    }
    uint32_t flags = furi_thread_flags_wait(ThreadEventStop, FuriFlagWaitAny | FuriFlagNoClear, 0);
    if(flags & FuriFlagError) {
        return;
    }
    if(flags & ThreadEventStop) {
        mjs_exit(mjs);
    }
}

bool js_thread_account(struct mjs* mjs, const JsLimitsUsage* increment) {
    JsThread* worker = mjs ? mjs_get_context(mjs) : NULL;
    if(!worker || !js_limits_account(&worker->limits, increment)) {
        if(mjs) mjs_exit(mjs);
        return false;
    }
    return true;
}

bool js_delay_with_flags(struct mjs* mjs, uint32_t time) {
    uint32_t flags =
        furi_thread_flags_wait(ThreadEventStop, FuriFlagWaitAny | FuriFlagNoClear, time);
    if(flags & FuriFlagError) {
        return false;
    }
    if(flags & ThreadEventStop) {
        mjs_exit(mjs);
        return true;
    }
    return false;
}

void js_flags_set(struct mjs* mjs, uint32_t flags) {
    JsThread* worker = mjs_get_context(mjs);
    furi_assert(worker);
    furi_thread_flags_set(furi_thread_get_id(worker->thread), flags);
}

uint32_t js_flags_wait(struct mjs* mjs, uint32_t flags_mask, uint32_t timeout) {
    flags_mask |= ThreadEventStop;
    uint32_t flags = furi_thread_flags_get();
    furi_check((flags & FuriFlagError) == 0);
    if(flags == 0) {
        flags = furi_thread_flags_wait(flags_mask, FuriFlagWaitAny | FuriFlagNoClear, timeout);
    } else {
        uint32_t state = furi_thread_flags_clear(flags & flags_mask);
        furi_check((state & FuriFlagError) == 0);
    }

    if(flags & FuriFlagError) {
        return 0;
    }
    if(flags & ThreadEventStop) {
        mjs_exit(mjs);
    }
    return flags;
}

static void js_delay(struct mjs* mjs) {
    bool args_correct = false;
    int ms = 0;

    if(mjs_nargs(mjs) == 1) {
        mjs_val_t arg = mjs_arg(mjs, 0);
        if(mjs_is_number(arg)) {
            ms = mjs_get_int(mjs, arg);
            args_correct = true;
        }
    }
    if(!args_correct) {
        mjs_prepend_errorf(mjs, MJS_BAD_ARGS_ERROR, "");
        mjs_return(mjs, MJS_UNDEFINED);
        return;
    }
    js_delay_with_flags(mjs, ms);
    mjs_return(mjs, MJS_UNDEFINED);
}

static void* js_dlsym(void* handle, const char* name) {
    CompositeApiResolver* resolver = handle;
    Elf32_Addr addr = 0;
    uint32_t hash = elf_symbolname_hash(name);
    const ElfApiInterface* api = composite_api_resolver_get(resolver);

    if(!api->resolver_callback(api, hash, &addr)) {
        FURI_LOG_E(TAG, "FFI: cannot find \"%s\"", name);
        return NULL;
    }

    return (void*)addr;
}

static void js_ffi_address(struct mjs* mjs) {
    mjs_val_t name_v = mjs_arg(mjs, 0);
    size_t len;
    const char* name = mjs_get_string(mjs, &name_v, &len);
    void* addr = mjs_ffi_resolve(mjs, name);
    mjs_return(mjs, mjs_mk_foreign(mjs, addr));
}

static void js_require(struct mjs* mjs) {
    mjs_val_t name_v = mjs_arg(mjs, 0);
    size_t len;
    const char* name = mjs_get_string(mjs, &name_v, &len);
    mjs_val_t req_object = MJS_UNDEFINED;
    if((len == 0) || (name == NULL)) {
        mjs_prepend_errorf(mjs, MJS_BAD_ARGS_ERROR, "String argument is expected");
    } else {
        JsThread* worker = mjs_get_context(mjs);
        furi_assert(worker);
        req_object = js_module_require(worker->modules, name, len);
    }
    mjs_return(mjs, req_object);
}

static void js_require_from(struct mjs* mjs) {
    mjs_val_t name_v = mjs_arg(mjs, 0);
    mjs_val_t base_v = mjs_arg(mjs, 1);
    size_t name_len = 0u;
    size_t base_len = 0u;
    const char* name = mjs_get_string(mjs, &name_v, &name_len);
    const char* base = mjs_get_string(mjs, &base_v, &base_len);
    mjs_val_t result = MJS_UNDEFINED;
    if(!name || name_len == 0u || !base) {
        mjs_prepend_errorf(mjs, MJS_BAD_ARGS_ERROR, "module name and base are required");
    } else {
        JsThread* worker = mjs_get_context(mjs);
        furi_assert(worker);
        result = js_module_require_from(worker->modules, name, name_len, base, base_len);
    }
    mjs_return(mjs, result);
}

static void js_async_acquire(struct mjs* mjs) {
    JsThread* worker = mjs_get_context(mjs);
    furi_assert(worker);
    if(worker->async_handles == UINT16_MAX) {
        mjs_prepend_errorf(mjs, MJS_BAD_ARGS_ERROR, "too many asynchronous handles");
        mjs_return(mjs, MJS_UNDEFINED);
        return;
    }
    ++worker->async_handles;
    mjs_return(mjs, MJS_UNDEFINED);
}

static void js_async_release(struct mjs* mjs) {
    JsThread* worker = mjs_get_context(mjs);
    furi_assert(worker);
    if(worker->async_handles == 0u) {
        mjs_prepend_errorf(mjs, MJS_TYPE_ERROR, "asynchronous handle released twice");
        mjs_return(mjs, MJS_UNDEFINED);
        return;
    }
    --worker->async_handles;
    mjs_return(mjs, mjs_mk_boolean(mjs, worker->async_handles == 0u));
}

static void js_runtime_fail(struct mjs* mjs) {
    mjs_val_t message_value = mjs_arg(mjs, 0);
    size_t message_length = 0u;
    const char* message = mjs_get_string(mjs, &message_value, &message_length);
    mjs_prepend_errorf(
        mjs,
        MJS_TYPE_ERROR,
        "%.*s",
        (int)message_length,
        message ? message : "JavaScript runtime failure");
    mjs_return(mjs, MJS_UNDEFINED);
}

static void js_parse_int(struct mjs* mjs) {
    static const JsValueDeclaration js_parse_int_arg_list[] = {
        JS_VALUE_SIMPLE(JsValueTypeString),
        JS_VALUE_SIMPLE_W_DEFAULT(JsValueTypeInt32, int32_val, 10),
    };
    static const JsValueArguments js_parse_int_args = JS_VALUE_ARGS(js_parse_int_arg_list);

    const char* str;
    int32_t base;
    JS_VALUE_PARSE_ARGS_OR_RETURN(mjs, &js_parse_int_args, &str, &base);

    int32_t num;
    if(strint_to_int32(str, NULL, &num, base) != StrintParseNoError) {
        num = 0;
    }
    mjs_return(mjs, mjs_mk_number(mjs, num));
}

#ifdef JS_DEBUG
static void js_dump_write_callback(void* ctx, const char* format, ...) {
    File* file = ctx;
    furi_assert(ctx);

    FuriString* str = furi_string_alloc();

    va_list args;
    va_start(args, format);
    furi_string_vprintf(str, format, args);
    furi_string_cat(str, "\n");
    va_end(args);

    storage_file_write(file, furi_string_get_cstr(str), furi_string_size(str));
    furi_string_free(str);
}
#endif

static struct mjs* js_runtime_alloc(JsThread* worker) {
    worker->resolver = composite_api_resolver_alloc();
    if(!worker->resolver) return NULL;
    composite_api_resolver_add(worker->resolver, firmware_api_interface);
    composite_api_resolver_add(worker->resolver, application_api_interface);

    struct mjs* mjs = mjs_create(worker);
    if(!mjs) {
        composite_api_resolver_free(worker->resolver);
        worker->resolver = NULL;
        return NULL;
    }
    mjs_set_parser_depth_limit(mjs, worker->limits.config.parser_depth);
    worker->modules = js_modules_create(
        mjs,
        worker->resolver,
        worker->granted_capabilities,
        worker->enforce_capabilities,
        furi_string_get_cstr(worker->path));
    if(!worker->modules) {
        if(worker->app_callback) {
            worker->app_callback(
                JsThreadEventError, "invalid managed project entrypoint", worker->context);
        }
        mjs_destroy(mjs);
        composite_api_resolver_free(worker->resolver);
        worker->resolver = NULL;
        return NULL;
    }
    mjs_val_t global = mjs_get_global(mjs);
    mjs_val_t console_obj = mjs_mk_object(mjs);

    if(worker->path) {
        FuriString* dirpath = furi_string_alloc();
        path_extract_dirname(furi_string_get_cstr(worker->path), dirpath);
        mjs_set(
            mjs,
            global,
            "__filename",
            ~0,
            mjs_mk_string(
                mjs, furi_string_get_cstr(worker->path), furi_string_size(worker->path), true));
        mjs_set(
            mjs,
            global,
            "__dirname",
            ~0,
            mjs_mk_string(mjs, furi_string_get_cstr(dirpath), furi_string_size(dirpath), true));
        furi_string_free(dirpath);
    }

    JS_ASSIGN_MULTI(mjs, global) {
        JS_FIELD("print", MJS_MK_FN(js_print));
        JS_FIELD("delay", MJS_MK_FN(js_delay));
        JS_FIELD("parseInt", MJS_MK_FN(js_parse_int));
        JS_FIELD("require", MJS_MK_FN(js_require));
        JS_FIELD("__poison_require_from", MJS_MK_FN(js_require_from));
        JS_FIELD("console", console_obj);

        JS_FIELD("sdkCompatibilityStatus", MJS_MK_FN(js_sdk_compatibility_status));
        JS_FIELD("isSdkCompatible", MJS_MK_FN(js_is_sdk_compatible));
        JS_FIELD("checkSdkCompatibility", MJS_MK_FN(js_check_sdk_compatibility));
        JS_FIELD("doesSdkSupport", MJS_MK_FN(js_does_sdk_support));
        JS_FIELD("checkSdkFeatures", MJS_MK_FN(js_check_sdk_features));
    }

    if(worker->enforce_capabilities) {
        mjs_set(mjs, global, "__poison_async_acquire", ~0, MJS_MK_FN(js_async_acquire));
        mjs_set(mjs, global, "__poison_async_release", ~0, MJS_MK_FN(js_async_release));
        mjs_set(mjs, global, "__poison_fail", ~0, MJS_MK_FN(js_runtime_fail));
    }

    if(worker->allow_native_ffi) {
        mjs_set(mjs, global, "ffi_address", ~0, MJS_MK_FN(js_ffi_address));
        mjs_set_ffi_resolver(mjs, js_dlsym, worker->resolver);
    }

    JS_ASSIGN_MULTI(mjs, console_obj) {
        JS_FIELD("log", MJS_MK_FN(js_console_log));
        JS_FIELD("warn", MJS_MK_FN(js_console_warn));
        JS_FIELD("error", MJS_MK_FN(js_console_error));
        JS_FIELD("debug", MJS_MK_FN(js_console_debug));
    }

    mjs_set_exec_flags_poller(mjs, js_exit_flag_poll);

    return mjs;
}

static void js_runtime_free(JsThread* worker, struct mjs* mjs) {
    if(worker->modules) {
        js_modules_destroy(worker->modules);
        worker->modules = NULL;
    }
    if(mjs) mjs_destroy(mjs);
    if(worker->resolver) {
        composite_api_resolver_free(worker->resolver);
        worker->resolver = NULL;
    }
}

#define JS_CLI_LINE_CAPACITY       512u
#define JS_DEBUG_BREAKPOINTS_MAX   32u
#define JS_DEBUG_SOURCE_LINE_BYTES 256u

typedef struct {
    char filename[JS_PROJECT_PATH_MAX];
    int line;
} JsDebugBreakpoint;

typedef enum {
    JsDebugRunContinue,
    JsDebugRunStep,
    JsDebugRunNext,
} JsDebugRunMode;

typedef struct {
    PipeSide* pipe;
    JsDebugBreakpoint breakpoints[JS_DEBUG_BREAKPOINTS_MAX];
    size_t breakpoint_count;
    JsDebugRunMode run_mode;
    size_t next_depth;
    bool stop_on_entry;
    bool quit;
    char current_file[JS_PROJECT_PATH_MAX];
    int current_line;
} JsDebugSession;

static bool js_cli_read_line(PipeSide* pipe, char* line, size_t capacity) {
    furi_assert(pipe);
    furi_assert(line);
    furi_assert(capacity > 1u);
    size_t length = 0u;
    while(pipe_state(pipe) == PipeStateOpen) {
        char c = '\0';
        if(pipe_receive(pipe, &c, 1u) != 1u) return false;
        if(c == CliKeyETX) {
            pipe_send(pipe, "\r\n", 2u);
            return false;
        }
        if(c == '\r' || c == '\n') {
            if(c == '\n' && length == 0u) continue;
            pipe_send(pipe, "\r\n", 2u);
            while(length > 0u && line[length - 1u] == ' ')
                --length;
            size_t leading = 0u;
            while(leading < length && line[leading] == ' ')
                ++leading;
            if(leading) {
                memmove(line, line + leading, length - leading);
                length -= leading;
            }
            line[length] = '\0';
            return true;
        }
        if(c == '\b' || c == 0x7f) {
            if(length) {
                --length;
                pipe_send(pipe, "\b \b", 3u);
            }
            continue;
        }
        if((unsigned char)c < 0x20u || length + 1u >= capacity) continue;
        line[length++] = c;
        pipe_send(pipe, &c, 1u);
    }
    return false;
}

static void js_cli_runtime_callback(JsThreadEvent event, const char* message, void* context) {
    UNUSED(context);
    if(event == JsThreadEventPrint && message) printf("%s\r\n", message);
}

static bool js_interactive_runtime_open(
    JsThread* worker,
    const char* path,
    PipeSide* pipe,
    struct mjs** mjs) {
    memset(worker, 0, sizeof(*worker));
    worker->thread = furi_thread_get_current();
    worker->path = furi_string_alloc_set_str(path);
    worker->app_callback = js_cli_runtime_callback;
    worker->allow_native_ffi = true;
    worker->interactive_pipe = pipe;
    js_limits_init(&worker->limits, NULL, furi_get_tick());
    *mjs = js_runtime_alloc(worker);
    if(*mjs) return true;
    furi_string_free(worker->path);
    worker->path = NULL;
    return false;
}

static void js_interactive_runtime_close(JsThread* worker, struct mjs* mjs) {
    js_runtime_free(worker, mjs);
    furi_string_free(worker->path);
    worker->path = NULL;
}

static void js_debug_print_value(struct mjs* mjs, mjs_val_t value) {
    char output[JS_DEBUG_SOURCE_LINE_BYTES];
    mjs_sprintf(value, mjs, output, sizeof(output));
    printf("%s\r\n", output);
}

static void js_debug_print_location(const char* filename, int line) {
    printf("Paused at %s:%d\r\n", filename ? filename : "<unknown>", line);
    if(!filename || filename[0] != '/') return;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, filename, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char source[JS_DEBUG_SOURCE_LINE_BYTES];
        size_t length = 0u;
        int current_line = 1;
        char c = '\0';
        while(storage_file_read(file, &c, 1u) == 1u) {
            if(current_line == line && c != '\r' && c != '\n' && length + 1u < sizeof(source))
                source[length++] = c;
            if(c == '\n') {
                if(current_line == line) break;
                ++current_line;
            }
        }
        source[length] = '\0';
        if(current_line == line) printf("%d | %s\r\n", line, source);
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static bool js_debug_breakpoint_matches(
    const JsDebugBreakpoint* breakpoint,
    const char* filename,
    int line) {
    if(breakpoint->line != line || !filename) return false;
    if(strcmp(breakpoint->filename, filename) == 0) return true;
    const char* expected_name = strrchr(breakpoint->filename, '/');
    const char* actual_name = strrchr(filename, '/');
    return strcmp(
               expected_name ? expected_name + 1u : breakpoint->filename,
               actual_name ? actual_name + 1u : filename) == 0;
}

static bool
    js_debug_has_breakpoint(const JsDebugSession* session, const char* filename, int line) {
    for(size_t index = 0u; index < session->breakpoint_count; ++index) {
        if(js_debug_breakpoint_matches(&session->breakpoints[index], filename, line)) return true;
    }
    return false;
}

static bool js_debug_parse_location(
    const JsDebugSession* session,
    const char* argument,
    char filename[JS_PROJECT_PATH_MAX],
    int* line) {
    while(*argument == ' ')
        ++argument;
    if(!*argument) return false;
    const char* separator = strrchr(argument, ':');
    const char* line_text = argument;
    if(separator) {
        const size_t filename_length = (size_t)(separator - argument);
        if(filename_length == 0u || filename_length >= JS_PROJECT_PATH_MAX) return false;
        memcpy(filename, argument, filename_length);
        filename[filename_length] = '\0';
        line_text = separator + 1u;
    } else {
        strlcpy(filename, session->current_file, JS_PROJECT_PATH_MAX);
    }
    char* end = NULL;
    const long parsed = strtol(line_text, &end, 10);
    if(!end || *end != '\0' || parsed <= 0 || parsed > INT_MAX) return false;
    *line = (int)parsed;
    return true;
}

static void js_debug_list_breakpoints(const JsDebugSession* session) {
    if(session->breakpoint_count == 0u) {
        printf("No breakpoints.\r\n");
        return;
    }
    for(size_t index = 0u; index < session->breakpoint_count; ++index) {
        printf(
            "%u: %s:%d\r\n",
            (unsigned)(index + 1u),
            session->breakpoints[index].filename,
            session->breakpoints[index].line);
    }
}

static void js_debug_add_breakpoint(JsDebugSession* session, const char* argument) {
    JsDebugBreakpoint breakpoint;
    if(!js_debug_parse_location(session, argument, breakpoint.filename, &breakpoint.line)) {
        printf("Usage: break [file:]line\r\n");
        return;
    }
    if(js_debug_has_breakpoint(session, breakpoint.filename, breakpoint.line)) {
        printf("Breakpoint already exists.\r\n");
        return;
    }
    if(session->breakpoint_count >= JS_DEBUG_BREAKPOINTS_MAX) {
        printf("Breakpoint limit reached (%u).\r\n", JS_DEBUG_BREAKPOINTS_MAX);
        return;
    }
    session->breakpoints[session->breakpoint_count++] = breakpoint;
    printf("Breakpoint set at %s:%d\r\n", breakpoint.filename, breakpoint.line);
}

static void js_debug_delete_breakpoint(JsDebugSession* session, const char* argument) {
    while(*argument == ' ')
        ++argument;
    char* end = NULL;
    const long number = strtol(argument, &end, 10);
    if(!end || *end != '\0' || number <= 0 || (size_t)number > session->breakpoint_count) {
        printf("Usage: delete <breakpoint-number>\r\n");
        return;
    }
    const size_t index = (size_t)number - 1u;
    for(size_t cursor = index; cursor + 1u < session->breakpoint_count; ++cursor)
        session->breakpoints[cursor] = session->breakpoints[cursor + 1u];
    --session->breakpoint_count;
    printf("Breakpoint %ld deleted.\r\n", number);
}

static void js_debug_backtrace(struct mjs* mjs) {
    for(int frame = 0;; ++frame) {
        const int offset = mjs_get_offset_by_call_frame_num(mjs, frame);
        if(offset < 0) break;
        const char* filename = mjs_get_bcode_filename_by_offset(mjs, offset);
        printf(
            "#%d %s:%d\r\n",
            frame,
            filename ? filename : "<unknown>",
            mjs_get_lineno_by_offset(mjs, offset));
    }
}

static void js_debug_locals(struct mjs* mjs) {
    bool any = false;
    for(size_t depth = 0u;; ++depth) {
        const mjs_val_t scope = mjs_debug_get_scope(mjs, depth);
        if(scope == MJS_UNDEFINED) break;
        mjs_val_t iterator = MJS_UNDEFINED;
        mjs_val_t key;
        while((key = mjs_next(mjs, scope, &iterator)) != MJS_UNDEFINED) {
            size_t key_length = 0u;
            const char* key_text = mjs_get_string(mjs, &key, &key_length);
            if(!key_text) continue;
            char value[JS_DEBUG_SOURCE_LINE_BYTES];
            mjs_sprintf(mjs_get_v(mjs, scope, key), mjs, value, sizeof(value));
            printf("%.*s = %s\r\n", (int)key_length, key_text, value);
            any = true;
        }
    }
    if(!any) printf("No visible bindings.\r\n");
}

static void js_debug_eval_expression(struct mjs* mjs, const char* expression) {
    while(*expression == ' ')
        ++expression;
    if(!*expression) {
        printf("Usage: print <expression>\r\n");
        return;
    }
    mjs_val_t result = MJS_UNDEFINED;
    char error[JS_DEBUG_SOURCE_LINE_BYTES];
    const mjs_err_t status = mjs_debug_eval(mjs, expression, &result, error, sizeof(error));
    if(status == MJS_OK)
        js_debug_print_value(mjs, result);
    else
        printf("%s\r\n", error);
}

static void js_debug_help(void) {
    printf("continue|c              run until a breakpoint or debugger statement\r\n"
           "step|s                  stop on the next source line\r\n"
           "next|n                  step over function calls\r\n"
           "break|b [file:]line     add a source breakpoint\r\n"
           "delete <number>         remove a breakpoint\r\n"
           "breakpoints             list breakpoints\r\n"
           "print|p <expression>    evaluate in the paused runtime\r\n"
           "locals                  print visible bindings\r\n"
           "where|bt                print the call stack\r\n"
           "quit|q                  terminate the script\r\n");
}

static size_t js_debug_call_depth(struct mjs* mjs) {
    size_t depth = 0u;
    while(mjs_get_offset_by_call_frame_num(mjs, (int)depth) >= 0)
        ++depth;
    return depth;
}

static void js_debug_hook(
    struct mjs* mjs,
    mjs_debug_event_t event,
    const char* filename,
    int line,
    void* context) {
    JsDebugSession* session = context;
    JsThread* worker = mjs_get_context(mjs);
    const size_t call_depth = js_debug_call_depth(mjs);
    const bool should_stop =
        session->stop_on_entry || event == MJS_DEBUG_EVENT_STATEMENT ||
        (worker && worker->debug_break_requested) || session->run_mode == JsDebugRunStep ||
        (session->run_mode == JsDebugRunNext && call_depth <= session->next_depth) ||
        js_debug_has_breakpoint(session, filename, line);
    if(!should_stop) return;

    if(worker) worker->debug_break_requested = false;
    session->stop_on_entry = false;
    session->run_mode = JsDebugRunContinue;
    session->current_line = line;
    strlcpy(
        session->current_file, filename ? filename : "<unknown>", sizeof(session->current_file));
    js_debug_print_location(filename, line);

    char command[JS_CLI_LINE_CAPACITY];
    while(!session->quit) {
        printf("poison-js(debug)> ");
        if(!js_cli_read_line(session->pipe, command, sizeof(command))) {
            session->quit = true;
            break;
        }
        char* argument = strchr(command, ' ');
        if(argument) {
            *argument++ = '\0';
            while(*argument == ' ')
                ++argument;
        } else {
            argument = command + strlen(command);
        }
        if(strcmp(command, "continue") == 0 || strcmp(command, "c") == 0) {
            session->run_mode = JsDebugRunContinue;
            return;
        } else if(strcmp(command, "step") == 0 || strcmp(command, "s") == 0) {
            session->run_mode = JsDebugRunStep;
            return;
        } else if(strcmp(command, "next") == 0 || strcmp(command, "n") == 0) {
            session->run_mode = JsDebugRunNext;
            session->next_depth = call_depth;
            return;
        } else if(strcmp(command, "break") == 0 || strcmp(command, "b") == 0) {
            js_debug_add_breakpoint(session, argument);
        } else if(strcmp(command, "delete") == 0) {
            js_debug_delete_breakpoint(session, argument);
        } else if(strcmp(command, "breakpoints") == 0) {
            js_debug_list_breakpoints(session);
        } else if(strcmp(command, "print") == 0 || strcmp(command, "p") == 0) {
            js_debug_eval_expression(mjs, argument);
        } else if(strcmp(command, "locals") == 0) {
            js_debug_locals(mjs);
        } else if(strcmp(command, "where") == 0 || strcmp(command, "bt") == 0) {
            js_debug_backtrace(mjs);
        } else if(strcmp(command, "help") == 0 || strcmp(command, "?") == 0) {
            js_debug_help();
        } else if(strcmp(command, "quit") == 0 || strcmp(command, "q") == 0) {
            session->quit = true;
            break;
        } else if(command[0] != '\0') {
            printf("Unknown debugger command. Enter help for commands.\r\n");
        }
    }
    mjs_exit(mjs);
}

void js_thread_cli_console(PipeSide* pipe) {
    JsThread worker;
    struct mjs* mjs = NULL;
    if(!js_interactive_runtime_open(&worker, "<console>", pipe, &mjs)) {
        printf("Unable to initialize JavaScript runtime.\r\n");
        return;
    }

    printf("Poisoned OS JavaScript console. Enter .help for commands.\r\n");
    char source[JS_CLI_LINE_CAPACITY];
    while(pipe_state(pipe) == PipeStateOpen) {
        printf("poison-js> ");
        if(!js_cli_read_line(pipe, source, sizeof(source)) || strcmp(source, ".exit") == 0) break;
        if(strcmp(source, ".help") == 0) {
            printf(".help                  show console commands\r\n"
                   ".exit                  close the console\r\n"
                   "Any other input is evaluated in one persistent runtime.\r\n");
            continue;
        }
        if(source[0] == '\0') continue;
        mjs_val_t result = MJS_UNDEFINED;
        char error[JS_DEBUG_SOURCE_LINE_BYTES];
        const mjs_err_t status = mjs_debug_eval(mjs, source, &result, error, sizeof(error));
        if(worker.interactive_interrupted) {
            worker.interactive_interrupted = false;
            printf("Execution interrupted.\r\n");
        } else if(status == MJS_OK)
            js_debug_print_value(mjs, result);
        else
            printf("%s\r\n", error);
    }
    js_interactive_runtime_close(&worker, mjs);
}

void js_thread_cli_debug(PipeSide* pipe, const char* script_path) {
    JsThread worker;
    struct mjs* mjs = NULL;
    if(!js_interactive_runtime_open(&worker, script_path, pipe, &mjs)) {
        printf("Unable to initialize JavaScript runtime.\r\n");
        return;
    }

    JsDebugSession* session = calloc(1u, sizeof(*session));
    if(!session) {
        printf("Unable to allocate JavaScript debug session.\r\n");
        js_interactive_runtime_close(&worker, mjs);
        return;
    }
    session->pipe = pipe;
    session->run_mode = JsDebugRunContinue;
    session->stop_on_entry = true;
    worker.interactive_debug = true;
    mjs_set_debug_hook(mjs, js_debug_hook, session);
    printf("Debugging %s. Execution will pause at the first source line.\r\n", script_path);
    const mjs_err_t status = mjs_exec_file(mjs, script_path, NULL);
    mjs_set_debug_hook(mjs, NULL, NULL);
    if(session->quit) {
        printf("Debug session terminated.\r\n");
    } else if(worker.interactive_interrupted) {
        printf("Debug session interrupted.\r\n");
    } else if(status != MJS_OK) {
        printf("%s\r\n", mjs_strerror(mjs, status));
        const char* trace = mjs_get_stack_trace(mjs);
        if(trace) printf("%s", trace);
    } else {
        printf("Script completed.\r\n");
    }
    free(session);
    js_interactive_runtime_close(&worker, mjs);
}

static int32_t js_thread(void* arg) {
    JsThread* worker = arg;
    struct mjs* mjs = js_runtime_alloc(worker);
    if(!mjs) return 0;

    if(worker->limits.config.source_bytes != 0u) {
        Storage* storage = furi_record_open(RECORD_STORAGE);
        FileInfo info;
        const bool source_valid =
            storage_common_stat(storage, furi_string_get_cstr(worker->path), &info) == FSE_OK &&
            !file_info_is_dir(&info) && info.size <= UINT32_MAX;
        furi_record_close(RECORD_STORAGE);
        const JsLimitsUsage source = {
            .source_bytes = source_valid ? (uint32_t)info.size : UINT32_MAX,
        };
        if(!source_valid || !js_thread_account(mjs, &source)) {
            if(worker->app_callback)
                worker->app_callback(
                    JsThreadEventError, "javascript source limit exceeded", worker->context);
            js_runtime_free(worker, mjs);
            return 0;
        }
    }

    mjs_err_t err = mjs_exec_file(mjs, furi_string_get_cstr(worker->path), NULL);
    while(err == MJS_OK && worker->async_handles > 0u && !js_limits_triggered(&worker->limits)) {
        const uint32_t flags = furi_thread_flags_get();
        if((flags & FuriFlagError) == 0u && (flags & ThreadEventStop)) break;
        err = js_modules_run_event_loop(worker->modules);
    }

#ifdef JS_DEBUG
    if(furi_hal_rtc_is_flag_set(FuriHalRtcFlagDebug)) {
        FuriString* dump_path = furi_string_alloc_set(worker->path);
        furi_string_cat(dump_path, ".lst");

        Storage* storage = furi_record_open(RECORD_STORAGE);
        File* file = storage_file_alloc(storage);

        if(storage_file_open(
               file, furi_string_get_cstr(dump_path), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            mjs_disasm_all(mjs, js_dump_write_callback, file);
        }

        storage_file_close(file);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);

        furi_string_free(dump_path);
    }
#endif

    if(js_limits_triggered(&worker->limits)) {
        const char* reason = js_limits_reason_text(js_limits_reason(&worker->limits));
        if(worker->app_callback) worker->app_callback(JsThreadEventError, reason, worker->context);
    } else if(err != MJS_OK) {
        FURI_LOG_E(TAG, "Exec error: %s", mjs_strerror(mjs, err));
        if(worker->app_callback) {
            worker->app_callback(JsThreadEventError, mjs_strerror(mjs, err), worker->context);
        }
        const char* stack_trace = mjs_get_stack_trace(mjs);
        if(stack_trace != NULL) {
            FURI_LOG_E(TAG, "Stack trace:\r\n%s", stack_trace);
            if(worker->app_callback) {
                worker->app_callback(JsThreadEventErrorTrace, stack_trace, worker->context);
            }
        }
    } else {
        if(worker->app_callback) {
            worker->app_callback(JsThreadEventDone, NULL, worker->context);
        }
    }

    js_runtime_free(worker, mjs);

    return 0;
}

static JsThread* js_thread_run_with_policy(
    const char* script_path,
    JsThreadCallback callback,
    void* context,
    const JsLimitsConfig* limits,
    uint32_t granted_capabilities,
    bool enforce_capabilities,
    bool allow_native_ffi) {
    JsThread* worker = calloc(1u, sizeof(JsThread)); //-V799
    if(!worker) return NULL;
    worker->path = furi_string_alloc_set(script_path);
    worker->thread = furi_thread_alloc_ex("JsThread", 8 * 1024, js_thread, worker);
    if(!worker->path || !worker->thread) {
        if(worker->thread) furi_thread_free(worker->thread);
        if(worker->path) furi_string_free(worker->path);
        free(worker);
        return NULL;
    }
    worker->app_callback = callback;
    worker->context = context;
    worker->granted_capabilities = granted_capabilities;
    worker->enforce_capabilities = enforce_capabilities;
    worker->allow_native_ffi = allow_native_ffi;
    js_limits_init(&worker->limits, limits, furi_get_tick());
    furi_thread_start(worker->thread);
    return worker;
}

JsThread* js_thread_run(const char* script_path, JsThreadCallback callback, void* context) {
    return js_thread_run_limited(script_path, callback, context, NULL);
}

JsThread* js_thread_run_limited(
    const char* script_path,
    JsThreadCallback callback,
    void* context,
    const JsLimitsConfig* limits) {
    return js_thread_run_with_policy(
        script_path, callback, context, limits, UINT32_MAX, false, true);
}

JsThread* js_thread_run_managed(
    const char* script_path,
    JsThreadCallback callback,
    void* context,
    const JsLimitsConfig* limits,
    uint32_t granted_capabilities) {
    return js_thread_run_with_policy(
        script_path, callback, context, limits, granted_capabilities, true, false);
}

void js_thread_stop(JsThread* worker) {
    furi_thread_flags_set(furi_thread_get_id(worker->thread), ThreadEventStop);
    furi_thread_join(worker->thread);
    furi_thread_free(worker->thread);
    furi_string_free(worker->path);
    free(worker);
}
