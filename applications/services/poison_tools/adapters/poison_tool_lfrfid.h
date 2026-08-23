#pragma once

#include "../poison_lfrfid_adapter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_TOOL_LFRFID_TIMEOUT_MAX_MS (60000u)
#define POISON_TOOL_LFRFID_RESULT_MAX     (192u)

typedef enum {
    PoisonToolLfRfidOperationRead,
    PoisonToolLfRfidOperationWrite,
    PoisonToolLfRfidOperationEmulate,
    PoisonToolLfRfidOperationCount,
} PoisonToolLfRfidOperation;

typedef enum {
    PoisonToolLfRfidCapabilityRead = 1u << 0,
    PoisonToolLfRfidCapabilityWrite = 1u << 1,
    PoisonToolLfRfidCapabilityEmulate = 1u << 2,
} PoisonToolLfRfidCapability;

typedef struct {
    PoisonToolLfRfidOperation operation;
    uint32_t timeout_ms;
    bool exact_confirmation;
} PoisonToolLfRfidRequest;

typedef enum {
    PoisonToolLfRfidRunOk,
    PoisonToolLfRfidRunInvalid,
    PoisonToolLfRfidRunBusy,
    PoisonToolLfRfidRunTimeout,
    PoisonToolLfRfidRunCancelled,
} PoisonToolLfRfidRunResult;

typedef bool (*PoisonToolLfRfidCancelCallback)(void* context);

bool poison_tool_lfrfid_request_validate(
    const PoisonToolLfRfidRequest* request,
    uint32_t granted_capabilities);

bool poison_tool_lfrfid_detection_json(
    const PoisonLfRfidDetection* detection,
    char* output,
    size_t output_capacity);

PoisonToolLfRfidRunResult poison_tool_lfrfid_read(
    const PoisonToolLfRfidRequest* request,
    uint32_t granted_capabilities,
    PoisonToolLfRfidCancelCallback cancelled,
    void* cancel_context,
    char* output,
    size_t output_capacity);

const char* poison_tool_lfrfid_run_result_name(PoisonToolLfRfidRunResult result);

#ifdef __cplusplus
}
#endif
