#pragma once

#include "js_thread.h"

#include <furi.h>
#include <mjs_core_public.h>
#include <mjs_ffi_public.h>
#include <mjs_exec_public.h>
#include <mjs_object_public.h>
#include <mjs_string_public.h>
#include <mjs_array_public.h>
#include <mjs_util_public.h>
#include <mjs_primitive_public.h>
#include <mjs_array_buf_public.h>
#include <toolbox/pipe.h>
#include "js_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INST_PROP_NAME "_"

typedef enum {
    ThreadEventStop = (1 << 0),
    ThreadEventCustomDataRx = (1 << 1),
} WorkerEventFlags;

bool js_delay_with_flags(struct mjs* mjs, uint32_t time);

void js_flags_set(struct mjs* mjs, uint32_t flags);

uint32_t js_flags_wait(struct mjs* mjs, uint32_t flags, uint32_t timeout);

JsThread* js_thread_run_managed(
    const char* script_path,
    JsThreadCallback callback,
    void* context,
    const JsLimitsConfig* limits,
    uint32_t granted_capabilities);

void js_thread_cli_console(PipeSide* pipe);

void js_thread_cli_debug(PipeSide* pipe, const char* script_path);

#ifdef __cplusplus
}
#endif
