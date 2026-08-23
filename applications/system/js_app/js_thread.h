#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "js_limits.h"

struct mjs;

typedef struct JsThread JsThread;

typedef enum {
    JsThreadEventDone,
    JsThreadEventError,
    JsThreadEventPrint,
    JsThreadEventErrorTrace,
} JsThreadEvent;

typedef void (*JsThreadCallback)(JsThreadEvent event, const char* msg, void* context);

JsThread* js_thread_run(const char* script_path, JsThreadCallback callback, void* context);

JsThread* js_thread_run_limited(
    const char* script_path,
    JsThreadCallback callback,
    void* context,
    const JsLimitsConfig* limits);

bool js_thread_account(struct mjs* mjs, const JsLimitsUsage* increment);

void js_thread_stop(JsThread* worker);

#ifdef __cplusplus
}
#endif
