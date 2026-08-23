#include "poison_tool_lfrfid.h"

#include <mbedtls/sha256.h>

#include <stdio.h>
#include <string.h>

#define POISON_TOOL_LFRFID_WAIT_SLICE_MS (50u)

static uint32_t poison_tool_lfrfid_required_capability(PoisonToolLfRfidOperation operation) {
    switch(operation) {
    case PoisonToolLfRfidOperationRead:
        return PoisonToolLfRfidCapabilityRead;
    case PoisonToolLfRfidOperationWrite:
        return PoisonToolLfRfidCapabilityWrite;
    case PoisonToolLfRfidOperationEmulate:
        return PoisonToolLfRfidCapabilityEmulate;
    default:
        return 0u;
    }
}

bool poison_tool_lfrfid_request_validate(
    const PoisonToolLfRfidRequest* request,
    uint32_t granted_capabilities) {
    if(!request || request->operation >= PoisonToolLfRfidOperationCount ||
       request->timeout_ms == 0u || request->timeout_ms > POISON_TOOL_LFRFID_TIMEOUT_MAX_MS) {
        return false;
    }
    const uint32_t required = poison_tool_lfrfid_required_capability(request->operation);
    if(required == 0u || (granted_capabilities & required) != required) return false;
    return request->operation == PoisonToolLfRfidOperationRead || request->exact_confirmation;
}

static bool poison_tool_lfrfid_protocol_valid(const char* protocol) {
    if(!protocol || protocol[0] == '\0' || strnlen(protocol, 33u) > 32u) return false;
    for(const char* cursor = protocol; *cursor; ++cursor) {
        if(!((*cursor >= 'A' && *cursor <= 'Z') || (*cursor >= 'a' && *cursor <= 'z') ||
             (*cursor >= '0' && *cursor <= '9') || *cursor == ' ' || *cursor == '-' ||
             *cursor == '/')) {
            return false;
        }
    }
    return true;
}

bool poison_tool_lfrfid_detection_json(
    const PoisonLfRfidDetection* detection,
    char* output,
    size_t output_capacity) {
    if(!detection || !output || output_capacity == 0u ||
       !poison_tool_lfrfid_protocol_valid(detection->protocol) || detection->data_size == 0u ||
       detection->data_size > POISON_LFRFID_DATA_MAX) {
        return false;
    }

    uint8_t digest[32u];
    if(mbedtls_sha256(detection->data, detection->data_size, digest, 0) != 0) return false;
    char digest_hex[65u];
    static const char hex[] = "0123456789abcdef";
    for(size_t index = 0u; index < sizeof(digest); ++index) {
        digest_hex[index * 2u] = hex[digest[index] >> 4u];
        digest_hex[index * 2u + 1u] = hex[digest[index] & 0x0fu];
    }
    digest_hex[64u] = '\0';
    memset(digest, 0, sizeof(digest));

    const int written = snprintf(
        output,
        output_capacity,
        "{\"protocol\":\"%s\",\"data_size\":%lu,\"sha256\":\"%s\",\"redacted\":true}",
        detection->protocol,
        (unsigned long)detection->data_size,
        digest_hex);
    memset(digest_hex, 0, sizeof(digest_hex));
    return written > 0 && (size_t)written < output_capacity;
}

PoisonToolLfRfidRunResult poison_tool_lfrfid_read(
    const PoisonToolLfRfidRequest* request,
    uint32_t granted_capabilities,
    PoisonToolLfRfidCancelCallback cancelled,
    void* cancel_context,
    char* output,
    size_t output_capacity) {
    if(!poison_tool_lfrfid_request_validate(request, granted_capabilities) ||
       request->operation != PoisonToolLfRfidOperationRead || !output || output_capacity == 0u) {
        return PoisonToolLfRfidRunInvalid;
    }

    PoisonLfRfidSession* session = poison_lfrfid_session_alloc();
    if(!session) return PoisonToolLfRfidRunBusy;
    if(!poison_lfrfid_session_start(session)) {
        poison_lfrfid_session_free(session);
        return PoisonToolLfRfidRunBusy;
    }

    PoisonToolLfRfidRunResult result = PoisonToolLfRfidRunTimeout;
    PoisonLfRfidDetection detection;
    uint32_t remaining = request->timeout_ms;
    while(remaining > 0u) {
        if(cancelled && cancelled(cancel_context)) {
            result = PoisonToolLfRfidRunCancelled;
            break;
        }
        const uint32_t slice = remaining < POISON_TOOL_LFRFID_WAIT_SLICE_MS ?
                                   remaining :
                                   POISON_TOOL_LFRFID_WAIT_SLICE_MS;
        if(poison_lfrfid_session_wait(session, slice, &detection)) {
            result = poison_tool_lfrfid_detection_json(&detection, output, output_capacity) ?
                         PoisonToolLfRfidRunOk :
                         PoisonToolLfRfidRunInvalid;
            break;
        }
        remaining -= slice;
    }
    poison_lfrfid_session_free(session);
    return result;
}

const char* poison_tool_lfrfid_run_result_name(PoisonToolLfRfidRunResult result) {
    static const char* const names[] = {
        [PoisonToolLfRfidRunOk] = "ok",
        [PoisonToolLfRfidRunInvalid] = "invalid-request",
        [PoisonToolLfRfidRunBusy] = "lf-rfid-busy",
        [PoisonToolLfRfidRunTimeout] = "lf-rfid-timeout",
        [PoisonToolLfRfidRunCancelled] = "cancelled",
    };
    return result <= PoisonToolLfRfidRunCancelled ? names[result] : NULL;
}
