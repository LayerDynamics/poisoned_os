#pragma once

#include "poison_workload.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    PoisonWorkload* workload;
    bool running;
} PoisonWorkloadNativeAdapter;

bool poison_workload_native_start(
    PoisonWorkloadNativeAdapter* adapter,
    PoisonWorkload* workload,
    const char* fap_path);
bool poison_workload_native_cancel(PoisonWorkloadNativeAdapter* adapter);
void poison_workload_native_cleanup(PoisonWorkloadNativeAdapter* adapter);

#ifdef __cplusplus
}
#endif
