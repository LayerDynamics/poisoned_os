#include "poison_tool_ibutton.h"

#include <furi.h>
#include <mbedtls/sha256.h>

#include <stdio.h>
#include <string.h>

#define POISON_TOOL_IBUTTON_WAIT_SLICE_MS (100u)

static uint32_t poison_tool_ibutton_required_capability(PoisonToolIButtonOperation operation) {
    switch(operation) {
    case PoisonToolIButtonOperationRead:
        return PoisonToolIButtonCapabilityRead;
    case PoisonToolIButtonOperationWrite:
        return PoisonToolIButtonCapabilityWrite;
    case PoisonToolIButtonOperationEmulate:
        return PoisonToolIButtonCapabilityEmulate;
    default:
        return 0u;
    }
}

bool poison_tool_ibutton_request_validate(
    const PoisonToolIButtonRequest* request,
    uint32_t granted_capabilities) {
    if(!request || request->operation >= PoisonToolIButtonOperationCount ||
       request->timeout_ms == 0u || request->timeout_ms > POISON_TOOL_IBUTTON_TIMEOUT_MAX_MS) {
        return false;
    }
    const uint32_t required = poison_tool_ibutton_required_capability(request->operation);
    if(required == 0u || (granted_capabilities & required) != required) return false;
    return request->operation == PoisonToolIButtonOperationRead || request->exact_confirmation;
}

static bool poison_tool_ibutton_protocol_valid(const char* protocol) {
    if(!protocol || protocol[0] == '\0' || strnlen(protocol, 33u) > 32u) return false;
    for(const char* cursor = protocol; *cursor; ++cursor) {
        if(!((*cursor >= 'A' && *cursor <= 'Z') || (*cursor >= 'a' && *cursor <= 'z') ||
             (*cursor >= '0' && *cursor <= '9') || *cursor == ' ' || *cursor == '-' ||
             *cursor == '/' || *cursor == '_')) {
            return false;
        }
    }
    return true;
}

bool poison_tool_ibutton_result_json(
    const PoisonIbuttonReadResult* result,
    char* output,
    size_t output_capacity) {
    if(!result || !output || output_capacity == 0u ||
       !poison_tool_ibutton_protocol_valid(result->protocol_name) || result->data_size == 0u ||
       result->data_size > sizeof(result->data) || result->rendered_size == 0u ||
       result->rendered_size >= sizeof(result->rendered) ||
       strnlen(result->rendered, sizeof(result->rendered)) != result->rendered_size) {
        return false;
    }

    uint8_t digest[32u];
    if(mbedtls_sha256(result->data, result->data_size, digest, 0) != 0) {
        return false;
    }
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
        "{\"protocol\":\"%s\",\"data_size\":%lu,\"valid\":%s,\"sha256\":\"%s\",\"redacted\":true}",
        result->protocol_name,
        (unsigned long)result->data_size,
        result->valid ? "true" : "false",
        digest_hex);
    memset(digest_hex, 0, sizeof(digest_hex));
    return written > 0 && (size_t)written < output_capacity;
}

PoisonToolIButtonRunResult poison_tool_ibutton_read(
    const PoisonToolIButtonRequest* request,
    uint32_t granted_capabilities,
    PoisonToolIButtonCancelCallback cancelled,
    void* cancel_context,
    PoisonIbuttonReadResult* evidence,
    char* output,
    size_t output_capacity) {
    if(!poison_tool_ibutton_request_validate(request, granted_capabilities) ||
       request->operation != PoisonToolIButtonOperationRead || !evidence || !output ||
       output_capacity == 0u) {
        return PoisonToolIButtonRunInvalid;
    }

    memset(evidence, 0, sizeof(*evidence));
    PoisonIbuttonHandle* handle = poison_ibutton_open();
    if(!handle) return PoisonToolIButtonRunBusy;
    PoisonToolIButtonRunResult run_result = PoisonToolIButtonRunTimeout;
    uint32_t remaining = request->timeout_ms;
    while(remaining > 0u) {
        if(cancelled && cancelled(cancel_context)) {
            run_result = PoisonToolIButtonRunCancelled;
            break;
        }
        if(poison_ibutton_read(handle, evidence)) {
            run_result = poison_tool_ibutton_result_json(evidence, output, output_capacity) ?
                             PoisonToolIButtonRunOk :
                             PoisonToolIButtonRunInvalid;
            break;
        }
        const uint32_t slice = remaining < POISON_TOOL_IBUTTON_WAIT_SLICE_MS ?
                                   remaining :
                                   POISON_TOOL_IBUTTON_WAIT_SLICE_MS;
        furi_delay_ms(slice);
        remaining -= slice;
    }
    poison_ibutton_close(handle);
    if(run_result != PoisonToolIButtonRunOk) memset(evidence, 0, sizeof(*evidence));
    return run_result;
}

const char* poison_tool_ibutton_run_result_name(PoisonToolIButtonRunResult result) {
    static const char* const names[] = {
        [PoisonToolIButtonRunOk] = "ok",
        [PoisonToolIButtonRunInvalid] = "invalid-request",
        [PoisonToolIButtonRunBusy] = "ibutton-busy",
        [PoisonToolIButtonRunTimeout] = "ibutton-timeout",
        [PoisonToolIButtonRunCancelled] = "cancelled",
    };
    return result <= PoisonToolIButtonRunCancelled ? names[result] : NULL;
}
