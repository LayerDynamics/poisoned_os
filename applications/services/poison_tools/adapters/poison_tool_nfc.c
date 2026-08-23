#include "poison_tool_nfc.h"

#include <stdio.h>
#include <string.h>

#define POISON_TOOL_NFC_WAIT_SLICE_MS (50u)

static uint32_t poison_tool_nfc_required_capability(PoisonToolNfcOperation operation) {
    switch(operation) {
    case PoisonToolNfcOperationDetect:
        return PoisonToolNfcCapabilityRead;
    case PoisonToolNfcOperationRawCapture:
        return PoisonToolNfcCapabilityRawCapture;
    case PoisonToolNfcOperationWrite:
        return PoisonToolNfcCapabilityWrite;
    case PoisonToolNfcOperationEmulate:
        return PoisonToolNfcCapabilityEmulate;
    default:
        return 0u;
    }
}

bool poison_tool_nfc_request_validate(
    const PoisonToolNfcRequest* request,
    uint32_t granted_capabilities) {
    if(!request || request->operation >= PoisonToolNfcOperationCount ||
       request->timeout_ms == 0u || request->timeout_ms > POISON_TOOL_NFC_TIMEOUT_MAX_MS) {
        return false;
    }
    const uint32_t required = poison_tool_nfc_required_capability(request->operation);
    if(required == 0u || (granted_capabilities & required) != required) return false;

    if(request->operation == PoisonToolNfcOperationDetect) {
        return request->maximum_capture_bytes == 0u;
    }
    if(request->maximum_capture_bytes == 0u ||
       request->maximum_capture_bytes > POISON_TOOL_NFC_RAW_CAPTURE_MAX) {
        return false;
    }
    if((request->operation == PoisonToolNfcOperationWrite ||
        request->operation == PoisonToolNfcOperationEmulate) &&
       !request->exact_confirmation) {
        return false;
    }
    return true;
}

bool poison_tool_nfc_detection_json(
    const PoisonNfcDetection* detection,
    char* output,
    size_t output_capacity) {
    if(!detection || !output || output_capacity == 0u || detection->protocol_count == 0u ||
       detection->protocol_count > NfcProtocolNum) {
        return false;
    }
    int written = snprintf(output, output_capacity, "{\"protocols\":[");
    if(written < 0 || (size_t)written >= output_capacity) return false;
    size_t used = (size_t)written;
    for(size_t index = 0u; index < detection->protocol_count; ++index) {
        const char* name = poison_nfc_protocol_name(detection->protocols[index]);
        if(!name) return false;
        written = snprintf(
            output + used, output_capacity - used, "%s\"%s\"", index == 0u ? "" : ",", name);
        if(written < 0 || (size_t)written >= output_capacity - used) return false;
        used += (size_t)written;
    }
    written = snprintf(output + used, output_capacity - used, "]}");
    return written >= 0 && (size_t)written < output_capacity - used;
}

PoisonToolNfcRunResult poison_tool_nfc_detect(
    const PoisonToolNfcRequest* request,
    uint32_t granted_capabilities,
    PoisonToolNfcCancelCallback cancelled,
    void* cancel_context,
    char* output,
    size_t output_capacity) {
    if(!poison_tool_nfc_request_validate(request, granted_capabilities) ||
       request->operation != PoisonToolNfcOperationDetect || !output || output_capacity == 0u) {
        return PoisonToolNfcRunInvalid;
    }

    PoisonNfcSession* session = poison_nfc_session_alloc();
    if(!session) return PoisonToolNfcRunBusy;
    if(!poison_nfc_session_start(session)) {
        poison_nfc_session_free(session);
        return PoisonToolNfcRunBusy;
    }

    PoisonToolNfcRunResult result = PoisonToolNfcRunTimeout;
    PoisonNfcDetection detection;
    uint32_t remaining = request->timeout_ms;
    while(remaining > 0u) {
        if(cancelled && cancelled(cancel_context)) {
            result = PoisonToolNfcRunCancelled;
            break;
        }
        const uint32_t slice =
            remaining < POISON_TOOL_NFC_WAIT_SLICE_MS ? remaining : POISON_TOOL_NFC_WAIT_SLICE_MS;
        if(poison_nfc_session_wait(session, slice, &detection)) {
            result = poison_tool_nfc_detection_json(&detection, output, output_capacity) ?
                         PoisonToolNfcRunOk :
                         PoisonToolNfcRunInvalid;
            break;
        }
        remaining -= slice;
    }
    poison_nfc_session_free(session);
    return result;
}

const char* poison_tool_nfc_run_result_name(PoisonToolNfcRunResult result) {
    static const char* const names[] = {
        [PoisonToolNfcRunOk] = "ok",
        [PoisonToolNfcRunInvalid] = "invalid-request",
        [PoisonToolNfcRunBusy] = "nfc-busy",
        [PoisonToolNfcRunTimeout] = "nfc-timeout",
        [PoisonToolNfcRunCancelled] = "cancelled",
    };
    return result <= PoisonToolNfcRunCancelled ? names[result] : NULL;
}
