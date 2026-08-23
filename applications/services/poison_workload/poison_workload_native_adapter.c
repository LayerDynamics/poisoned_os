#include "poison_workload_native_adapter.h"

#include <loader/loader.h>

#include <furi.h>

#include <string.h>

static bool poison_workload_native_path_valid(const char* path) {
    if(!path || path[0] == '\0' || strnlen(path, 256u) >= 256u) return false;
    if(strstr(path, "..") != NULL) return false;
    const size_t length = strlen(path);
    if(length < 5u || strcmp(path + length - 4u, ".fap") != 0) return false;
    return strncmp(path, "/ext/", 5u) == 0 || strncmp(path, "/int/", 5u) == 0;
}

bool poison_workload_native_start(
    PoisonWorkloadNativeAdapter* adapter,
    PoisonWorkload* workload,
    const char* fap_path) {
    if(!adapter || !workload || adapter->running || !poison_workload_native_path_valid(fap_path) ||
       !poison_workload_start(workload)) {
        return false;
    }
    Loader* loader = furi_record_open(RECORD_LOADER);
    if(!loader) {
        poison_workload_finish(workload, false);
        return false;
    }
    const LoaderStatus status = loader_start(loader, fap_path, NULL, NULL);
    furi_record_close(RECORD_LOADER);
    if(status != LoaderStatusOk) {
        poison_workload_finish(workload, false);
        return false;
    }
    adapter->workload = workload;
    adapter->running = true;
    return true;
}

bool poison_workload_native_cancel(PoisonWorkloadNativeAdapter* adapter) {
    if(!adapter || !adapter->running || !adapter->workload) return false;
    if(!poison_workload_request_cancel(adapter->workload)) return false;
    Loader* loader = furi_record_open(RECORD_LOADER);
    if(!loader) return false;
    const bool signalled = loader_signal(loader, FuriSignalExit, NULL);
    furi_record_close(RECORD_LOADER);
    if(!signalled) return false;
    PoisonWorkload* workload = adapter->workload;
    adapter->running = false;
    adapter->workload = NULL;
    return poison_workload_force_terminate(workload, PoisonWorkloadTerminalCancelled);
}

void poison_workload_native_cleanup(PoisonWorkloadNativeAdapter* adapter) {
    if(!adapter) return;
    if(adapter->running && adapter->workload) {
        Loader* loader = furi_record_open(RECORD_LOADER);
        if(loader) {
            (void)loader_signal(loader, FuriSignalExit, NULL);
            furi_record_close(RECORD_LOADER);
        }
    }
    adapter->workload = NULL;
    adapter->running = false;
}
