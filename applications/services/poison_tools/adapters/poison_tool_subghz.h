#pragma once

#include "../poison_subghz_adapter.h"
#include "../../rpc/poison_policy.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_TOOL_SUBGHZ_TIMEOUT_MAX_MS (60000u)
#define POISON_TOOL_SUBGHZ_EVIDENCE_MAX   (5376u)
#define POISON_TOOL_SUBGHZ_RESULT_MAX     (320u)

typedef enum {
    PoisonToolSubGhzOperationReceive,
    PoisonToolSubGhzOperationAnalyze,
    PoisonToolSubGhzOperationTransmit,
    PoisonToolSubGhzOperationCount,
} PoisonToolSubGhzOperation;

typedef enum {
    PoisonToolSubGhzCapabilityReceive = 1u << 0,
    PoisonToolSubGhzCapabilityAnalyze = 1u << 1,
    PoisonToolSubGhzCapabilityTransmit = 1u << 2,
} PoisonToolSubGhzCapability;

typedef struct {
    PoisonToolSubGhzOperation operation;
    uint32_t frequency_hz;
    uint32_t timeout_ms;
    size_t maximum_timings;
    bool exact_confirmation;
} PoisonToolSubGhzRequest;

typedef struct {
    bool hardware_frequency_supported;
    bool region_provisioned;
    bool region_frequency_allowed;
    bool profile_region_matches;
    bool tool_enabled;
    bool classroom_restricted;
    bool classroom_instructor;
    PoisonCapability role_capabilities;
    PoisonCapability profile_capabilities;
} PoisonToolSubGhzPolicySnapshot;

typedef enum {
    PoisonToolSubGhzRunOk,
    PoisonToolSubGhzRunInvalid,
    PoisonToolSubGhzRunBusy,
    PoisonToolSubGhzRunDenied,
    PoisonToolSubGhzRunTimeout,
    PoisonToolSubGhzRunCancelled,
    PoisonToolSubGhzRunOverflow,
    PoisonToolSubGhzRunTransmitFailed,
} PoisonToolSubGhzRunResult;

typedef bool (*PoisonToolSubGhzPolicyCallback)(
    const PoisonToolSubGhzRequest* request,
    PoisonToolSubGhzPolicySnapshot* snapshot,
    void* context);
typedef bool (*PoisonToolSubGhzCancelCallback)(void* context);

bool poison_tool_subghz_request_validate(
    const PoisonToolSubGhzRequest* request,
    uint32_t granted_capabilities);
bool poison_tool_subghz_policy_allows(
    const PoisonToolSubGhzRequest* request,
    const PoisonToolSubGhzPolicySnapshot* policy);
bool poison_tool_subghz_evidence_encode(
    const PoisonSubGhzResult* result,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_size);
bool poison_tool_subghz_result_json(
    const PoisonSubGhzResult* result,
    const uint8_t* evidence,
    size_t evidence_size,
    char* output,
    size_t output_capacity);

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
    size_t output_capacity);

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
    size_t output_capacity);

const char* poison_tool_subghz_run_result_name(PoisonToolSubGhzRunResult result);

#ifdef __cplusplus
}
#endif
