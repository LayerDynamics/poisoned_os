#pragma once

#include "../poison_nfc_adapter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_TOOL_NFC_TIMEOUT_MAX_MS  (60000u)
#define POISON_TOOL_NFC_RAW_CAPTURE_MAX (8192u)
#define POISON_TOOL_NFC_RESULT_MAX      (512u)

typedef enum {
    PoisonToolNfcOperationDetect,
    PoisonToolNfcOperationRawCapture,
    PoisonToolNfcOperationWrite,
    PoisonToolNfcOperationEmulate,
    PoisonToolNfcOperationCount,
} PoisonToolNfcOperation;

typedef enum {
    PoisonToolNfcCapabilityRead = 1u << 0,
    PoisonToolNfcCapabilityRawCapture = 1u << 1,
    PoisonToolNfcCapabilityWrite = 1u << 2,
    PoisonToolNfcCapabilityEmulate = 1u << 3,
} PoisonToolNfcCapability;

typedef struct {
    PoisonToolNfcOperation operation;
    uint32_t timeout_ms;
    uint32_t maximum_capture_bytes;
    bool exact_confirmation;
} PoisonToolNfcRequest;

typedef enum {
    PoisonToolNfcRunOk,
    PoisonToolNfcRunInvalid,
    PoisonToolNfcRunBusy,
    PoisonToolNfcRunTimeout,
    PoisonToolNfcRunCancelled,
} PoisonToolNfcRunResult;

typedef bool (*PoisonToolNfcCancelCallback)(void* context);

bool poison_tool_nfc_request_validate(
    const PoisonToolNfcRequest* request,
    uint32_t granted_capabilities);

bool poison_tool_nfc_detection_json(
    const PoisonNfcDetection* detection,
    char* output,
    size_t output_capacity);

PoisonToolNfcRunResult poison_tool_nfc_detect(
    const PoisonToolNfcRequest* request,
    uint32_t granted_capabilities,
    PoisonToolNfcCancelCallback cancelled,
    void* cancel_context,
    char* output,
    size_t output_capacity);

const char* poison_tool_nfc_run_result_name(PoisonToolNfcRunResult result);

#ifdef __cplusplus
}
#endif
