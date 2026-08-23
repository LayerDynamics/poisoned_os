#include "poison_tool_subghz.h"

#include <furi.h>
#include <mbedtls/sha256.h>

#include <stdio.h>
#include <string.h>

#define POISON_TOOL_SUBGHZ_WAIT_SLICE_MS (50u)

static void poison_tool_subghz_u16(uint8_t* output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8u);
}

static void poison_tool_subghz_u32(uint8_t* output, uint32_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8u);
    output[2] = (uint8_t)(value >> 16u);
    output[3] = (uint8_t)(value >> 24u);
}

static bool poison_tool_subghz_digest(const uint8_t* data, size_t size, char output[65u]) {
    if(!data || size == 0u || !output) return false;
    uint8_t digest[32u];
    if(mbedtls_sha256(data, size, digest, 0) != 0) return false;
    static const char hex[] = "0123456789abcdef";
    for(size_t index = 0u; index < sizeof(digest); ++index) {
        output[index * 2u] = hex[digest[index] >> 4u];
        output[index * 2u + 1u] = hex[digest[index] & 0x0fu];
    }
    output[64u] = '\0';
    memset(digest, 0, sizeof(digest));
    return true;
}

bool poison_tool_subghz_request_validate(
    const PoisonToolSubGhzRequest* request,
    uint32_t granted_capabilities) {
    if(!request || request->operation >= PoisonToolSubGhzOperationCount ||
       request->frequency_hz == 0u || request->frequency_hz > 1000000000u ||
       request->timeout_ms == 0u || request->timeout_ms > POISON_TOOL_SUBGHZ_TIMEOUT_MAX_MS ||
       request->maximum_timings == 0u ||
       request->maximum_timings > POISON_SUBGHZ_RAW_TIMINGS_MAX) {
        return false;
    }
    if(request->operation == PoisonToolSubGhzOperationReceive)
        return (granted_capabilities & PoisonToolSubGhzCapabilityReceive) != 0u;
    if(request->operation == PoisonToolSubGhzOperationAnalyze)
        return (granted_capabilities & PoisonToolSubGhzCapabilityAnalyze) != 0u;
    return (granted_capabilities & PoisonToolSubGhzCapabilityTransmit) != 0u &&
           request->exact_confirmation;
}

bool poison_tool_subghz_policy_allows(
    const PoisonToolSubGhzRequest* request,
    const PoisonToolSubGhzPolicySnapshot* policy) {
    if(!request || !policy || !policy->hardware_frequency_supported ||
       !policy->profile_region_matches || !policy->tool_enabled)
        return false;
    PoisonCapability required = POISON_CAPABILITY_CONTROL | POISON_CAPABILITY_RADIO;
    if(request->operation == PoisonToolSubGhzOperationTransmit)
        required |= POISON_CAPABILITY_DESTRUCTIVE;
    if((policy->role_capabilities & required) != required ||
       (policy->profile_capabilities != 0u &&
        (policy->profile_capabilities & required) != required)) {
        return false;
    }
    if(request->operation == PoisonToolSubGhzOperationTransmit &&
       (!policy->region_provisioned || !policy->region_frequency_allowed)) {
        return false;
    }
    if(policy->classroom_restricted && request->operation != PoisonToolSubGhzOperationReceive &&
       !policy->classroom_instructor) {
        return false;
    }
    return true;
}

bool poison_tool_subghz_evidence_encode(
    const PoisonSubGhzResult* result,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_size) {
    if(!result || !output || !output_size || result->frequency == 0u || result->raw_count == 0u ||
       result->raw_count > POISON_SUBGHZ_RAW_TIMINGS_MAX || result->raw_overflow) {
        return false;
    }
    if(result->decoded_valid &&
       strnlen(result->decoded, sizeof(result->decoded)) >= sizeof(result->decoded)) {
        return false;
    }
    const size_t required = 16u + result->raw_count * 5u;
    if(required > output_capacity) return false;
    memcpy(output, "PSG1", 4u);
    size_t used = 4u;
    poison_tool_subghz_u32(output + used, result->frequency);
    used += 4u;
    poison_tool_subghz_u32(output + used, (uint32_t)(int32_t)(result->rssi * 1000.0f));
    used += 4u;
    output[used++] = result->lqi;
    output[used++] = result->decoded_valid ? 1u : 0u;
    poison_tool_subghz_u16(output + used, (uint16_t)result->raw_count);
    used += 2u;
    for(size_t index = 0u; index < result->raw_count; ++index) {
        const LevelDuration timing = result->raw_timings[index];
        if(level_duration_is_reset(timing) || level_duration_is_wait(timing) ||
           level_duration_get_duration(timing) == 0u) {
            return false;
        }
        output[used++] = level_duration_get_level(timing) ? 1u : 0u;
        poison_tool_subghz_u32(output + used, level_duration_get_duration(timing));
        used += 4u;
    }
    *output_size = used;
    return true;
}

bool poison_tool_subghz_result_json(
    const PoisonSubGhzResult* result,
    const uint8_t* evidence,
    size_t evidence_size,
    char* output,
    size_t output_capacity) {
    if(!result || !evidence || evidence_size == 0u || !output || output_capacity == 0u)
        return false;
    char raw_digest[65u];
    if(!poison_tool_subghz_digest(evidence, evidence_size, raw_digest)) return false;
    char derived_digest[65u] = {0};
    if(result->decoded_valid &&
       !poison_tool_subghz_digest(
           (const uint8_t*)result->decoded, strlen(result->decoded), derived_digest)) {
        memset(raw_digest, 0, sizeof(raw_digest));
        return false;
    }
    const int written = snprintf(
        output,
        output_capacity,
        "{\"frequency_hz\":%lu,\"rssi_milli\":%ld,\"lqi\":%u,\"raw_timings\":%lu,\"decoded\":%s,\"raw_sha256\":\"%s\",\"derived_sha256\":\"%s\"}",
        (unsigned long)result->frequency,
        (long)(result->rssi * 1000.0f),
        result->lqi,
        (unsigned long)result->raw_count,
        result->decoded_valid ? "true" : "false",
        raw_digest,
        result->decoded_valid ? derived_digest : "");
    memset(raw_digest, 0, sizeof(raw_digest));
    memset(derived_digest, 0, sizeof(derived_digest));
    return written > 0 && (size_t)written < output_capacity;
}

static bool poison_tool_subghz_live_policy(
    const PoisonToolSubGhzRequest* request,
    PoisonSubGhzHandle* handle,
    PoisonToolSubGhzPolicyCallback callback,
    void* context) {
    if(!callback) return false;
    PoisonToolSubGhzPolicySnapshot snapshot = {
        .hardware_frequency_supported =
            poison_subghz_frequency_valid(handle, request->frequency_hz),
    };
    return callback(request, &snapshot, context) &&
           poison_tool_subghz_policy_allows(request, &snapshot);
}

PoisonToolSubGhzRunResult poison_tool_subghz_receive(
    const PoisonToolSubGhzRequest* request,
    uint32_t granted_capabilities,
    PoisonToolSubGhzPolicyCallback policy,
    void* policy_context,
    PoisonToolSubGhzCancelCallback cancelled,
    void* cancel_context,
    PoisonSubGhzResult* result,
    uint8_t* evidence,
    size_t evidence_capacity,
    size_t* evidence_size,
    char* output,
    size_t output_capacity) {
    if(!poison_tool_subghz_request_validate(request, granted_capabilities) ||
       request->operation == PoisonToolSubGhzOperationTransmit || !result || !evidence ||
       !evidence_size || !output || output_capacity == 0u) {
        return PoisonToolSubGhzRunInvalid;
    }
    memset(result, 0, sizeof(*result));
    *evidence_size = 0u;
    if(cancelled && cancelled(cancel_context)) return PoisonToolSubGhzRunCancelled;
    PoisonSubGhzHandle* handle = poison_subghz_open(request->frequency_hz);
    if(!handle) return PoisonToolSubGhzRunBusy;
    if(!poison_tool_subghz_live_policy(request, handle, policy, policy_context)) {
        poison_subghz_close(handle);
        return PoisonToolSubGhzRunDenied;
    }
    PoisonToolSubGhzRunResult run_result = PoisonToolSubGhzRunTimeout;
    uint32_t remaining = request->timeout_ms;
    while(remaining > 0u) {
        if(cancelled && cancelled(cancel_context)) {
            run_result = PoisonToolSubGhzRunCancelled;
            break;
        }
        const uint32_t slice = remaining < POISON_TOOL_SUBGHZ_WAIT_SLICE_MS ?
                                   remaining :
                                   POISON_TOOL_SUBGHZ_WAIT_SLICE_MS;
        const bool received = poison_subghz_receive(handle, slice, result);
        if(result->raw_overflow) {
            run_result = PoisonToolSubGhzRunOverflow;
            break;
        }
        if(received) {
            if(result->raw_count > request->maximum_timings) {
                run_result = PoisonToolSubGhzRunOverflow;
            } else if(
                poison_tool_subghz_evidence_encode(
                    result, evidence, evidence_capacity, evidence_size) &&
                poison_tool_subghz_result_json(
                    result, evidence, *evidence_size, output, output_capacity)) {
                run_result = PoisonToolSubGhzRunOk;
            } else {
                run_result = PoisonToolSubGhzRunInvalid;
            }
            break;
        }
        remaining -= slice;
    }
    poison_subghz_close(handle);
    if(run_result != PoisonToolSubGhzRunOk) {
        memset(result, 0, sizeof(*result));
        memset(evidence, 0, evidence_capacity);
        *evidence_size = 0u;
    }
    return run_result;
}

PoisonToolSubGhzRunResult poison_tool_subghz_transmit(
    const PoisonToolSubGhzRequest* request,
    uint32_t granted_capabilities,
    PoisonToolSubGhzPolicyCallback policy,
    void* policy_context,
    PoisonToolSubGhzCancelCallback cancelled,
    void* cancel_context,
    const PoisonSubGhzResult* signal,
    uint8_t* evidence,
    size_t evidence_capacity,
    size_t* evidence_size,
    char* output,
    size_t output_capacity) {
    if(!poison_tool_subghz_request_validate(request, granted_capabilities) ||
       request->operation != PoisonToolSubGhzOperationTransmit || !signal || !evidence ||
       !evidence_size || !output || output_capacity == 0u ||
       signal->frequency != request->frequency_hz ||
       signal->raw_count > request->maximum_timings) {
        return PoisonToolSubGhzRunInvalid;
    }
    *evidence_size = 0u;
    if(cancelled && cancelled(cancel_context)) return PoisonToolSubGhzRunCancelled;
    if(!poison_tool_subghz_evidence_encode(signal, evidence, evidence_capacity, evidence_size)) {
        return PoisonToolSubGhzRunInvalid;
    }
    PoisonSubGhzHandle* handle = poison_subghz_open(request->frequency_hz);
    if(!handle) {
        memset(evidence, 0, evidence_capacity);
        *evidence_size = 0u;
        return PoisonToolSubGhzRunBusy;
    }
    if(!poison_tool_subghz_live_policy(request, handle, policy, policy_context)) {
        poison_subghz_close(handle);
        memset(evidence, 0, evidence_capacity);
        *evidence_size = 0u;
        return PoisonToolSubGhzRunDenied;
    }
    PoisonToolSubGhzRunResult run_result = PoisonToolSubGhzRunTransmitFailed;
    if(poison_subghz_transmit_raw(
           handle, signal->frequency, signal->raw_timings, signal->raw_count)) {
        uint32_t remaining = request->timeout_ms;
        while(remaining > 0u) {
            if(cancelled && cancelled(cancel_context)) {
                run_result = PoisonToolSubGhzRunCancelled;
                break;
            }
            if(poison_subghz_transmit_complete(handle)) {
                run_result = PoisonToolSubGhzRunOk;
                break;
            }
            const uint32_t slice = remaining < POISON_TOOL_SUBGHZ_WAIT_SLICE_MS ?
                                       remaining :
                                       POISON_TOOL_SUBGHZ_WAIT_SLICE_MS;
            furi_delay_ms(slice);
            remaining -= slice;
        }
        if(run_result == PoisonToolSubGhzRunTransmitFailed && remaining == 0u)
            run_result = PoisonToolSubGhzRunTimeout;
    }
    poison_subghz_close(handle);
    if(run_result == PoisonToolSubGhzRunOk) {
        char metadata[POISON_TOOL_SUBGHZ_RESULT_MAX];
        if(!poison_tool_subghz_result_json(
               signal, evidence, *evidence_size, metadata, sizeof(metadata))) {
            run_result = PoisonToolSubGhzRunInvalid;
        } else {
            const int written =
                snprintf(output, output_capacity, "{\"transmitted\":true,%s", metadata + 1u);
            if(written <= 0 || (size_t)written >= output_capacity)
                run_result = PoisonToolSubGhzRunInvalid;
        }
        memset(metadata, 0, sizeof(metadata));
    }
    if(run_result != PoisonToolSubGhzRunOk) {
        memset(evidence, 0, evidence_capacity);
        *evidence_size = 0u;
    }
    return run_result;
}

const char* poison_tool_subghz_run_result_name(PoisonToolSubGhzRunResult result) {
    static const char* const names[] = {
        [PoisonToolSubGhzRunOk] = "ok",
        [PoisonToolSubGhzRunInvalid] = "invalid-signal",
        [PoisonToolSubGhzRunBusy] = "sub-ghz-busy",
        [PoisonToolSubGhzRunDenied] = "sub-ghz-policy-denied",
        [PoisonToolSubGhzRunTimeout] = "sub-ghz-timeout",
        [PoisonToolSubGhzRunCancelled] = "cancelled",
        [PoisonToolSubGhzRunOverflow] = "sub-ghz-capture-overflow",
        [PoisonToolSubGhzRunTransmitFailed] = "sub-ghz-transmit-failed",
    };
    return result <= PoisonToolSubGhzRunTransmitFailed ? names[result] : NULL;
}
