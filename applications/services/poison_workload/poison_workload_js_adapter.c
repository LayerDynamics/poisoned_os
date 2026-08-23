#include "poison_workload_js_adapter.h"
#include "poison_workload_i.h"

#include <applications/system/js_app/js_thread_i.h>

#include <string.h>

static PoisonWorkloadTerminalReason poison_workload_js_limit_reason(const char* message) {
    if(!message) return PoisonWorkloadTerminalNone;
    static const struct {
        JsLimitReason js_reason;
        PoisonWorkloadTerminalReason workload_reason;
    } reasons[] = {
        {JsLimitReasonHeap, PoisonWorkloadTerminalHeapLimit},
        {JsLimitReasonSource, PoisonWorkloadTerminalSourceLimit},
        {JsLimitReasonModule, PoisonWorkloadTerminalModuleLimit},
        {JsLimitReasonParser, PoisonWorkloadTerminalParserLimit},
        {JsLimitReasonStack, PoisonWorkloadTerminalStackLimit},
        {JsLimitReasonFuel, PoisonWorkloadTerminalFuelLimit},
        {JsLimitReasonCallback, PoisonWorkloadTerminalCallbackLimit},
        {JsLimitReasonTimer, PoisonWorkloadTerminalTimerLimit},
        {JsLimitReasonHandle, PoisonWorkloadTerminalHandleLimit},
        {JsLimitReasonLog, PoisonWorkloadTerminalLogLimit},
        {JsLimitReasonArtifact, PoisonWorkloadTerminalArtifactLimit},
    };
    for(size_t index = 0u; index < sizeof(reasons) / sizeof(reasons[0]); ++index) {
        if(strcmp(message, js_limits_reason_text(reasons[index].js_reason)) == 0)
            return reasons[index].workload_reason;
    }
    return PoisonWorkloadTerminalNone;
}

static void poison_workload_js_event(JsThreadEvent event, const char* message, void* context) {
    PoisonWorkloadJsAdapter* adapter = context;
    if(!adapter || !adapter->workload) return;
    if(adapter->mutex)
        furi_check(furi_mutex_acquire(adapter->mutex, FuriWaitForever) == FuriStatusOk);
    PoisonWorkload* workload = adapter->workload;
    if(event == JsThreadEventPrint) {
        poison_workload_append_console(
            workload, PoisonWorkloadConsoleStdout, message ? message : "");
    } else if(
        (event == JsThreadEventError || event == JsThreadEventErrorTrace) &&
        workload->state != PoisonWorkloadCancelling) {
        const PoisonWorkloadTerminalReason limit_reason = poison_workload_js_limit_reason(message);
        if(limit_reason != PoisonWorkloadTerminalNone)
            poison_workload_force_terminate(workload, limit_reason);
        else {
            poison_workload_append_console(
                workload, PoisonWorkloadConsoleStderr, message ? message : "");
            poison_workload_finish(workload, false);
        }
    } else if(event == JsThreadEventDone && workload->state != PoisonWorkloadCancelling) {
        poison_workload_finish(workload, true);
    }
    if(adapter->mutex) furi_check(furi_mutex_release(adapter->mutex) == FuriStatusOk);
}

bool poison_workload_js_start(
    PoisonWorkloadJsAdapter* adapter,
    PoisonWorkload* workload,
    const char* script_path) {
    return poison_workload_js_start_managed(adapter, workload, script_path, UINT32_MAX, NULL);
}

bool poison_workload_js_start_managed(
    PoisonWorkloadJsAdapter* adapter,
    PoisonWorkload* workload,
    const char* script_path,
    uint32_t granted_capabilities,
    FuriMutex* mutex) {
    if(!adapter || !workload || !script_path || !poison_workload_start(workload)) return false;
    adapter->workload = workload;
    adapter->mutex = mutex;
    const JsLimitsConfig limits = {
        .heap_bytes = workload->limits.heap_bytes,
        .source_bytes = workload->limits.source_bytes,
        .modules = workload->limits.modules,
        .parser_depth = workload->limits.parser_depth,
        .stack_depth = workload->limits.stack_depth,
        .fuel_limit = workload->limits.fuel,
        .callbacks = workload->limits.callbacks,
        .timers = workload->limits.timers,
        .open_handles = workload->limits.open_handles,
        .logs = workload->limits.logs,
        .artifacts = workload->limits.artifacts,
        .wall_time_ms = workload->limits.wall_ms,
    };
    adapter->thread = js_thread_run_managed(
        script_path, poison_workload_js_event, adapter, &limits, granted_capabilities);
    if(!adapter->thread) {
        poison_workload_finish(workload, false);
        adapter->workload = NULL;
        adapter->mutex = NULL;
        return false;
    }
    return true;
}

bool poison_workload_js_cancel(PoisonWorkloadJsAdapter* adapter) {
    if(!adapter || !adapter->thread || !adapter->workload) return false;
    if(adapter->mutex)
        furi_check(furi_mutex_acquire(adapter->mutex, FuriWaitForever) == FuriStatusOk);
    const bool requested = poison_workload_request_cancel(adapter->workload);
    if(adapter->mutex) furi_check(furi_mutex_release(adapter->mutex) == FuriStatusOk);
    if(!requested) return false;
    js_thread_stop(adapter->thread);
    adapter->thread = NULL;
    if(adapter->mutex)
        furi_check(furi_mutex_acquire(adapter->mutex, FuriWaitForever) == FuriStatusOk);
    const bool terminated =
        poison_workload_force_terminate(adapter->workload, PoisonWorkloadTerminalCancelled);
    if(adapter->mutex) furi_check(furi_mutex_release(adapter->mutex) == FuriStatusOk);
    return terminated;
}

void poison_workload_js_cleanup(PoisonWorkloadJsAdapter* adapter) {
    if(!adapter) return;
    if(adapter->thread) {
        js_thread_stop(adapter->thread);
        adapter->thread = NULL;
    }
    adapter->workload = NULL;
    adapter->mutex = NULL;
}
