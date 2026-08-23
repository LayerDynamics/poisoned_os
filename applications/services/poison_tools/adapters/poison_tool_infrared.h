#pragma once

#include "../poison_infrared_adapter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_TOOL_INFRARED_TIMEOUT_MAX_MS (60000u)
#define POISON_TOOL_INFRARED_EVIDENCE_MAX   (4128u)
#define POISON_TOOL_INFRARED_RESULT_MAX     (256u)

typedef enum {
    PoisonToolInfraredOperationReceive,
    PoisonToolInfraredOperationTransmit,
    PoisonToolInfraredOperationCount,
} PoisonToolInfraredOperation;

typedef enum {
    PoisonToolInfraredCapabilityReceive = 1u << 0,
    PoisonToolInfraredCapabilityTransmit = 1u << 1,
} PoisonToolInfraredCapability;

typedef struct {
    PoisonToolInfraredOperation operation;
    uint32_t timeout_ms;
    size_t maximum_timings;
    bool exact_confirmation;
} PoisonToolInfraredRequest;

typedef enum {
    PoisonToolInfraredRunOk,
    PoisonToolInfraredRunInvalid,
    PoisonToolInfraredRunBusy,
    PoisonToolInfraredRunTimeout,
    PoisonToolInfraredRunCancelled,
    PoisonToolInfraredRunTransmitFailed,
} PoisonToolInfraredRunResult;

typedef bool (*PoisonToolInfraredCancelCallback)(void* context);

bool poison_tool_infrared_request_validate(
    const PoisonToolInfraredRequest* request,
    uint32_t granted_capabilities);

bool poison_tool_infrared_evidence_encode(
    const PoisonInfraredResult* result,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_size);

bool poison_tool_infrared_result_json(
    const PoisonInfraredResult* result,
    const uint8_t* evidence,
    size_t evidence_size,
    char* output,
    size_t output_capacity);

PoisonToolInfraredRunResult poison_tool_infrared_receive(
    const PoisonToolInfraredRequest* request,
    uint32_t granted_capabilities,
    PoisonToolInfraredCancelCallback cancelled,
    void* cancel_context,
    PoisonInfraredResult* result,
    uint8_t* evidence,
    size_t evidence_capacity,
    size_t* evidence_size,
    char* output,
    size_t output_capacity);

PoisonToolInfraredRunResult poison_tool_infrared_transmit(
    const PoisonToolInfraredRequest* request,
    uint32_t granted_capabilities,
    PoisonToolInfraredCancelCallback cancelled,
    void* cancel_context,
    const PoisonInfraredResult* signal,
    uint8_t* evidence,
    size_t evidence_capacity,
    size_t* evidence_size,
    char* output,
    size_t output_capacity);

const char* poison_tool_infrared_run_result_name(PoisonToolInfraredRunResult result);

#ifdef __cplusplus
}
#endif
