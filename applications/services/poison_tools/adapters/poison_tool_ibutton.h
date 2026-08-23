#pragma once

#include "../poison_ibutton_adapter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_TOOL_IBUTTON_TIMEOUT_MAX_MS (60000u)
#define POISON_TOOL_IBUTTON_RESULT_MAX     (192u)

typedef enum {
    PoisonToolIButtonOperationRead,
    PoisonToolIButtonOperationWrite,
    PoisonToolIButtonOperationEmulate,
    PoisonToolIButtonOperationCount,
} PoisonToolIButtonOperation;

typedef enum {
    PoisonToolIButtonCapabilityRead = 1u << 0,
    PoisonToolIButtonCapabilityWrite = 1u << 1,
    PoisonToolIButtonCapabilityEmulate = 1u << 2,
} PoisonToolIButtonCapability;

typedef struct {
    PoisonToolIButtonOperation operation;
    uint32_t timeout_ms;
    bool exact_confirmation;
} PoisonToolIButtonRequest;

typedef enum {
    PoisonToolIButtonRunOk,
    PoisonToolIButtonRunInvalid,
    PoisonToolIButtonRunBusy,
    PoisonToolIButtonRunTimeout,
    PoisonToolIButtonRunCancelled,
} PoisonToolIButtonRunResult;

typedef bool (*PoisonToolIButtonCancelCallback)(void* context);

bool poison_tool_ibutton_request_validate(
    const PoisonToolIButtonRequest* request,
    uint32_t granted_capabilities);

bool poison_tool_ibutton_result_json(
    const PoisonIbuttonReadResult* result,
    char* output,
    size_t output_capacity);

PoisonToolIButtonRunResult poison_tool_ibutton_read(
    const PoisonToolIButtonRequest* request,
    uint32_t granted_capabilities,
    PoisonToolIButtonCancelCallback cancelled,
    void* cancel_context,
    PoisonIbuttonReadResult* evidence,
    char* output,
    size_t output_capacity);

const char* poison_tool_ibutton_run_result_name(PoisonToolIButtonRunResult result);

#ifdef __cplusplus
}
#endif
