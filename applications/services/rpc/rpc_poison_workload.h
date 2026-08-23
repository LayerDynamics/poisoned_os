#pragma once

#include "poison_policy.h"

#include <poison_workload.pb.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool rpc_poison_workload_request_is_valid(
    const PB_Poison_WorkloadRequest* request,
    PoisonRole role);
uint32_t rpc_poison_workload_effective_js_capabilities(
    PoisonCapability session_capabilities,
    uint32_t requested_capabilities);

#ifdef __cplusplus
}
#endif
