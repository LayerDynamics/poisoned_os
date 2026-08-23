#include "../test.h"
#include "../../../../services/poison_tools/adapters/poison_tool_subghz.h"

#include <stdlib.h>
#include <string.h>

static bool poison_tool_subghz_test_cancelled(void* context) {
    return *(const bool*)context;
}

static bool poison_tool_subghz_test_contains(
    const uint8_t* haystack,
    size_t haystack_size,
    const uint8_t* needle,
    size_t needle_size) {
    if(!haystack || !needle || needle_size == 0u || needle_size > haystack_size) return false;
    for(size_t index = 0u; index <= haystack_size - needle_size; ++index) {
        if(memcmp(haystack + index, needle, needle_size) == 0) return true;
    }
    return false;
}

MU_TEST(poison_tool_subghz_separates_authority_and_rechecks_policy) {
    PoisonToolSubGhzRequest request = {
        .operation = PoisonToolSubGhzOperationReceive,
        .frequency_hz = 433920000u,
        .timeout_ms = 5000u,
        .maximum_timings = POISON_SUBGHZ_RAW_TIMINGS_MAX,
        .exact_confirmation = false,
    };
    mu_check(poison_tool_subghz_request_validate(&request, PoisonToolSubGhzCapabilityReceive));
    mu_check(!poison_tool_subghz_request_validate(&request, PoisonToolSubGhzCapabilityAnalyze));

    request.operation = PoisonToolSubGhzOperationAnalyze;
    mu_check(poison_tool_subghz_request_validate(&request, PoisonToolSubGhzCapabilityAnalyze));
    request.operation = PoisonToolSubGhzOperationTransmit;
    mu_check(!poison_tool_subghz_request_validate(&request, PoisonToolSubGhzCapabilityTransmit));
    request.exact_confirmation = true;
    mu_check(poison_tool_subghz_request_validate(&request, PoisonToolSubGhzCapabilityTransmit));

    PoisonToolSubGhzPolicySnapshot policy = {
        .hardware_frequency_supported = true,
        .region_provisioned = true,
        .region_frequency_allowed = true,
        .profile_region_matches = true,
        .tool_enabled = true,
        .classroom_restricted = false,
        .classroom_instructor = false,
        .role_capabilities = POISON_CAPABILITY_CONTROL | POISON_CAPABILITY_RADIO |
                             POISON_CAPABILITY_DESTRUCTIVE,
        .profile_capabilities = 0u,
    };
    mu_check(poison_tool_subghz_policy_allows(&request, &policy));
    policy.region_frequency_allowed = false;
    mu_check(!poison_tool_subghz_policy_allows(&request, &policy));
    policy.region_frequency_allowed = true;
    policy.classroom_restricted = true;
    mu_check(!poison_tool_subghz_policy_allows(&request, &policy));
    policy.classroom_instructor = true;
    mu_check(poison_tool_subghz_policy_allows(&request, &policy));
    policy.hardware_frequency_supported = false;
    mu_check(!poison_tool_subghz_policy_allows(&request, &policy));
    policy.hardware_frequency_supported = true;
    policy.profile_region_matches = false;
    mu_check(!poison_tool_subghz_policy_allows(&request, &policy));
}

MU_TEST(poison_tool_subghz_preserves_raw_and_derived_evidence) {
    PoisonSubGhzResult* result = malloc(sizeof(*result));
    mu_check(result);
    memset(result, 0, sizeof(*result));
    result->frequency = 433920000u;
    result->rssi = -42.5f;
    result->lqi = 91u;
    strcpy(result->decoded, "Princeton 24bit Key:00A1B2");
    result->decoded_valid = true;
    result->raw_count = 3u;
    result->raw_timings[0] = level_duration_make(true, 350u);
    result->raw_timings[1] = level_duration_make(false, 1050u);
    result->raw_timings[2] = level_duration_make(true, 350u);

    uint8_t* evidence = malloc(POISON_TOOL_SUBGHZ_EVIDENCE_MAX);
    mu_check(evidence);
    size_t evidence_size = 0u;
    mu_check(poison_tool_subghz_evidence_encode(
        result, evidence, POISON_TOOL_SUBGHZ_EVIDENCE_MAX, &evidence_size));
    mu_check(!poison_tool_subghz_test_contains(
        evidence, evidence_size, (const uint8_t*)result->decoded, strlen(result->decoded)));
    char json[POISON_TOOL_SUBGHZ_RESULT_MAX];
    mu_check(poison_tool_subghz_result_json(result, evidence, evidence_size, json, sizeof(json)));
    mu_check(strstr(json, "\"frequency_hz\":433920000") != NULL);
    mu_check(strstr(json, "\"raw_timings\":3") != NULL);
    mu_check(strstr(json, "\"decoded\":true") != NULL);
    mu_check(strstr(json, "\"sha256\":\"") != NULL);

    result->raw_overflow = true;
    mu_check(!poison_tool_subghz_evidence_encode(
        result, evidence, POISON_TOOL_SUBGHZ_EVIDENCE_MAX, &evidence_size));
    result->raw_overflow = false;
    result->raw_timings[1] = level_duration_reset();
    mu_check(!poison_tool_subghz_evidence_encode(
        result, evidence, POISON_TOOL_SUBGHZ_EVIDENCE_MAX, &evidence_size));
    memset(evidence, 0, POISON_TOOL_SUBGHZ_EVIDENCE_MAX);
    free(evidence);
    memset(result, 0, sizeof(*result));
    free(result);
}

MU_TEST(poison_tool_subghz_cancelled_replay_does_not_acquire_radio) {
    PoisonToolSubGhzRequest request = {
        .operation = PoisonToolSubGhzOperationTransmit,
        .frequency_hz = 433920000u,
        .timeout_ms = 5000u,
        .maximum_timings = 1u,
        .exact_confirmation = true,
    };
    PoisonSubGhzResult signal = {
        .frequency = 433920000u,
        .raw_count = 1u,
    };
    signal.raw_timings[0] = level_duration_make(true, 350u);
    const bool cancelled = true;
    uint8_t evidence[64u];
    size_t evidence_size = 0u;
    char json[POISON_TOOL_SUBGHZ_RESULT_MAX];
    mu_check(
        poison_tool_subghz_transmit(
            &request,
            PoisonToolSubGhzCapabilityTransmit,
            NULL,
            NULL,
            poison_tool_subghz_test_cancelled,
            (void*)&cancelled,
            &signal,
            evidence,
            sizeof(evidence),
            &evidence_size,
            json,
            sizeof(json)) == PoisonToolSubGhzRunCancelled);
    mu_check(evidence_size == 0u);
}

MU_TEST_SUITE(poison_tool_subghz_suite) {
    MU_RUN_TEST(poison_tool_subghz_separates_authority_and_rechecks_policy);
    MU_RUN_TEST(poison_tool_subghz_preserves_raw_and_derived_evidence);
    MU_RUN_TEST(poison_tool_subghz_cancelled_replay_does_not_acquire_radio);
}

void poison_tool_subghz_run_tests(void) {
    MU_RUN_SUITE(poison_tool_subghz_suite);
}
