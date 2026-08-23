#include "poison_tool_infrared.h"
#include "../poison_tools_i.h"

#include <mbedtls/sha256.h>

#include <stdio.h>
#include <string.h>

#define POISON_TOOL_INFRARED_WAIT_SLICE_MS (50u)

static void poison_tool_infrared_u16(uint8_t* output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8u);
}

static void poison_tool_infrared_u32(uint8_t* output, uint32_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8u);
    output[2] = (uint8_t)(value >> 16u);
    output[3] = (uint8_t)(value >> 24u);
}

bool poison_tool_infrared_request_validate(
    const PoisonToolInfraredRequest* request,
    uint32_t granted_capabilities) {
    if(!request || request->operation >= PoisonToolInfraredOperationCount ||
       request->timeout_ms == 0u || request->timeout_ms > POISON_TOOL_INFRARED_TIMEOUT_MAX_MS ||
       request->maximum_timings == 0u || request->maximum_timings > MAX_TIMINGS_AMOUNT) {
        return false;
    }
    if(request->operation == PoisonToolInfraredOperationReceive)
        return (granted_capabilities & PoisonToolInfraredCapabilityReceive) != 0u;
    return (granted_capabilities & PoisonToolInfraredCapabilityTransmit) != 0u &&
           request->exact_confirmation;
}

bool poison_tool_infrared_evidence_encode(
    const PoisonInfraredResult* result,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_size) {
    if(!result || !output || !output_size || output_capacity < 15u) return false;
    memcpy(output, "PIR1", 4u);
    size_t used = 4u;
    output[used++] = result->decoded ? 0u : 1u;
    if(result->decoded) {
        if(!infrared_is_protocol_valid(result->protocol)) return false;
        output[used++] = (uint8_t)result->protocol;
        output[used++] = result->repeat ? 1u : 0u;
        poison_tool_infrared_u32(output + used, result->address);
        used += 4u;
        poison_tool_infrared_u32(output + used, result->command);
        used += 4u;
    } else {
        if(result->timings == 0u || result->timings > MAX_TIMINGS_AMOUNT ||
           result->frequency == 0u || result->duty_cycle <= 0.0f || result->duty_cycle > 1.0f) {
            return false;
        }
        const size_t required = 13u + result->timings * sizeof(uint32_t);
        if(required > output_capacity) return false;
        poison_tool_infrared_u32(output + used, result->frequency);
        used += 4u;
        const uint16_t duty_milli = (uint16_t)(result->duty_cycle * 1000.0f + 0.5f);
        poison_tool_infrared_u16(output + used, duty_milli);
        used += 2u;
        poison_tool_infrared_u16(output + used, (uint16_t)result->timings);
        used += 2u;
        for(size_t index = 0u; index < result->timings; ++index) {
            if(result->raw_timings[index] == 0u) return false;
            poison_tool_infrared_u32(output + used, result->raw_timings[index]);
            used += 4u;
        }
    }
    *output_size = used;
    return true;
}

bool poison_tool_infrared_result_json(
    const PoisonInfraredResult* result,
    const uint8_t* evidence,
    size_t evidence_size,
    char* output,
    size_t output_capacity) {
    if(!result || !evidence || evidence_size == 0u || !output || output_capacity == 0u)
        return false;
    uint8_t digest[32u];
    if(mbedtls_sha256(evidence, evidence_size, digest, 0) != 0) return false;
    char digest_hex[65u];
    static const char hex[] = "0123456789abcdef";
    for(size_t index = 0u; index < sizeof(digest); ++index) {
        digest_hex[index * 2u] = hex[digest[index] >> 4u];
        digest_hex[index * 2u + 1u] = hex[digest[index] & 0x0fu];
    }
    digest_hex[64u] = '\0';
    memset(digest, 0, sizeof(digest));

    int written = 0;
    if(result->decoded) {
        const char* protocol = infrared_get_protocol_name(result->protocol);
        if(!protocol) return false;
        written = snprintf(
            output,
            output_capacity,
            "{\"decoded\":true,\"protocol\":\"%s\",\"address\":%lu,\"command\":%lu,\"repeat\":%s,\"sha256\":\"%s\"}",
            protocol,
            (unsigned long)result->address,
            (unsigned long)result->command,
            result->repeat ? "true" : "false",
            digest_hex);
    } else {
        written = snprintf(
            output,
            output_capacity,
            "{\"decoded\":false,\"timings\":%lu,\"frequency\":%lu,\"duty_cycle_milli\":%lu,\"sha256\":\"%s\"}",
            (unsigned long)result->timings,
            (unsigned long)result->frequency,
            (unsigned long)(result->duty_cycle * 1000.0f + 0.5f),
            digest_hex);
    }
    memset(digest_hex, 0, sizeof(digest_hex));
    return written > 0 && (size_t)written < output_capacity;
}

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
    size_t output_capacity) {
    if(!poison_tool_infrared_request_validate(request, granted_capabilities) ||
       request->operation != PoisonToolInfraredOperationReceive || !result || !evidence ||
       !evidence_size || !output || output_capacity == 0u) {
        return PoisonToolInfraredRunInvalid;
    }
    memset(result, 0, sizeof(*result));
    *evidence_size = 0u;
    PoisonInfraredHandle* handle = poison_infrared_open();
    if(!handle) return PoisonToolInfraredRunBusy;
    PoisonToolInfraredRunResult run_result = PoisonToolInfraredRunTimeout;
    uint32_t remaining = request->timeout_ms;
    while(remaining > 0u) {
        if(cancelled && cancelled(cancel_context)) {
            run_result = PoisonToolInfraredRunCancelled;
            break;
        }
        const uint32_t slice = remaining < POISON_TOOL_INFRARED_WAIT_SLICE_MS ?
                                   remaining :
                                   POISON_TOOL_INFRARED_WAIT_SLICE_MS;
        if(poison_infrared_receive(handle, slice, result)) {
            if(!result->decoded && result->timings > request->maximum_timings) {
                run_result = PoisonToolInfraredRunInvalid;
            } else if(
                poison_tool_infrared_evidence_encode(
                    result, evidence, evidence_capacity, evidence_size) &&
                poison_tool_infrared_result_json(
                    result, evidence, *evidence_size, output, output_capacity)) {
                run_result = PoisonToolInfraredRunOk;
            } else {
                run_result = PoisonToolInfraredRunInvalid;
            }
            break;
        }
        remaining -= slice;
    }
    poison_infrared_close(handle);
    if(run_result != PoisonToolInfraredRunOk) {
        memset(result, 0, sizeof(*result));
        memset(evidence, 0, evidence_capacity);
        *evidence_size = 0u;
    }
    return run_result;
}

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
    size_t output_capacity) {
    if(!poison_tool_infrared_request_validate(request, granted_capabilities) ||
       request->operation != PoisonToolInfraredOperationTransmit || !signal || !evidence ||
       !evidence_size || !output || output_capacity == 0u) {
        return PoisonToolInfraredRunInvalid;
    }
    *evidence_size = 0u;
    if(cancelled && cancelled(cancel_context)) return PoisonToolInfraredRunCancelled;
    if((!signal->decoded && signal->timings > request->maximum_timings) ||
       !poison_tool_infrared_evidence_encode(signal, evidence, evidence_capacity, evidence_size)) {
        return PoisonToolInfraredRunInvalid;
    }

    PoisonInfraredHandle* handle = poison_infrared_open();
    if(!handle) {
        memset(evidence, 0, evidence_capacity);
        *evidence_size = 0u;
        return PoisonToolInfraredRunBusy;
    }
    const bool transmitted =
        poison_infrared_load_result(handle, signal, request->maximum_timings) &&
        poison_infrared_transmit_once(handle);
    poison_infrared_close(handle);
    if(!transmitted) {
        memset(evidence, 0, evidence_capacity);
        *evidence_size = 0u;
        return PoisonToolInfraredRunTransmitFailed;
    }

    char metadata[POISON_TOOL_INFRARED_RESULT_MAX];
    if(!poison_tool_infrared_result_json(
           signal, evidence, *evidence_size, metadata, sizeof(metadata))) {
        memset(evidence, 0, evidence_capacity);
        *evidence_size = 0u;
        return PoisonToolInfraredRunInvalid;
    }
    const int written =
        snprintf(output, output_capacity, "{\"transmitted\":true,%s", metadata + 1u);
    memset(metadata, 0, sizeof(metadata));
    if(written <= 0 || (size_t)written >= output_capacity) {
        memset(evidence, 0, evidence_capacity);
        *evidence_size = 0u;
        return PoisonToolInfraredRunInvalid;
    }
    return PoisonToolInfraredRunOk;
}

const char* poison_tool_infrared_run_result_name(PoisonToolInfraredRunResult result) {
    static const char* const names[] = {
        [PoisonToolInfraredRunOk] = "ok",
        [PoisonToolInfraredRunInvalid] = "invalid-signal",
        [PoisonToolInfraredRunBusy] = "infrared-busy",
        [PoisonToolInfraredRunTimeout] = "infrared-timeout",
        [PoisonToolInfraredRunCancelled] = "cancelled",
        [PoisonToolInfraredRunTransmitFailed] = "infrared-transmit-failed",
    };
    return result <= PoisonToolInfraredRunTransmitFailed ? names[result] : NULL;
}
