#pragma once

#include "poison_workload.h"
#include <applications/system/js_app/js_thread.h>
#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    JsThread* thread;
    PoisonWorkload* workload;
    FuriMutex* mutex;
} PoisonWorkloadJsAdapter;

bool poison_workload_js_start(
    PoisonWorkloadJsAdapter* adapter,
    PoisonWorkload* workload,
    const char* script_path);
bool poison_workload_js_cancel(PoisonWorkloadJsAdapter* adapter);
void poison_workload_js_cleanup(PoisonWorkloadJsAdapter* adapter);

#ifdef __cplusplus
}
#endif
