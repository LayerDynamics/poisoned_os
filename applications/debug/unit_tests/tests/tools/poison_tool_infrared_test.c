#include "../test.h"
#include "../../../../services/poison_tools/adapters/poison_tool_infrared.h"
#include "../../../../services/poison_tools/poison_tools_i.h"

#include <stdlib.h>
#include <string.h>

static bool poison_tool_infrared_test_cancelled(void* context) {
    return *(const bool*)context;
}

MU_TEST(poison_tool_infrared_separates_receive_and_transmit_authority) {
    PoisonToolInfraredRequest request = {
        .operation = PoisonToolInfraredOperationReceive,
        .timeout_ms = 5000u,
        .maximum_timings = MAX_TIMINGS_AMOUNT,
        .exact_confirmation = false,
    };
    mu_check(poison_tool_infrared_request_validate(&request, PoisonToolInfraredCapabilityReceive));
    mu_check(
        !poison_tool_infrared_request_validate(&request, PoisonToolInfraredCapabilityTransmit));

    request.operation = PoisonToolInfraredOperationTransmit;
    mu_check(
        !poison_tool_infrared_request_validate(&request, PoisonToolInfraredCapabilityTransmit));
    request.exact_confirmation = true;
    mu_check(
        poison_tool_infrared_request_validate(&request, PoisonToolInfraredCapabilityTransmit));

    request.maximum_timings = MAX_TIMINGS_AMOUNT + 1u;
    mu_check(
        !poison_tool_infrared_request_validate(&request, PoisonToolInfraredCapabilityTransmit));
    request.maximum_timings = MAX_TIMINGS_AMOUNT;
    request.timeout_ms = POISON_TOOL_INFRARED_TIMEOUT_MAX_MS + 1u;
    mu_check(
        !poison_tool_infrared_request_validate(&request, PoisonToolInfraredCapabilityTransmit));
}

MU_TEST(poison_tool_infrared_encodes_complete_bounded_evidence) {
    PoisonInfraredResult* result = malloc(sizeof(*result));
    mu_check(result);
    memset(result, 0, sizeof(*result));
    result->decoded = false;
    result->timings = 3u;
    result->raw_timings[0] = 9000u;
    result->raw_timings[1] = 4500u;
    result->raw_timings[2] = 560u;
    result->frequency = 38000u;
    result->duty_cycle = 0.33f;

    uint8_t* evidence = malloc(POISON_TOOL_INFRARED_EVIDENCE_MAX);
    mu_check(evidence);
    size_t evidence_size = 0u;
    mu_check(poison_tool_infrared_evidence_encode(
        result, evidence, POISON_TOOL_INFRARED_EVIDENCE_MAX, &evidence_size));
    mu_check(evidence_size > 12u);
    char json[POISON_TOOL_INFRARED_RESULT_MAX];
    mu_check(
        poison_tool_infrared_result_json(result, evidence, evidence_size, json, sizeof(json)));
    mu_check(strstr(json, "\"decoded\":false") != NULL);
    mu_check(strstr(json, "\"timings\":3") != NULL);
    mu_check(strstr(json, "\"frequency\":38000") != NULL);
    mu_check(strstr(json, "\"sha256\":\"") != NULL);

    result->timings = MAX_TIMINGS_AMOUNT + 1u;
    mu_check(!poison_tool_infrared_evidence_encode(
        result, evidence, POISON_TOOL_INFRARED_EVIDENCE_MAX, &evidence_size));
    result->timings = 1u;
    result->raw_timings[0] = 0u;
    mu_check(!poison_tool_infrared_evidence_encode(
        result, evidence, POISON_TOOL_INFRARED_EVIDENCE_MAX, &evidence_size));
    result->raw_timings[0] = 100u;
    result->duty_cycle = 0.0f;
    mu_check(!poison_tool_infrared_evidence_encode(
        result, evidence, POISON_TOOL_INFRARED_EVIDENCE_MAX, &evidence_size));
    memset(evidence, 0, POISON_TOOL_INFRARED_EVIDENCE_MAX);
    free(evidence);
    memset(result, 0, sizeof(*result));
    free(result);
}

MU_TEST(poison_tool_infrared_replay_denial_and_cancel_never_touch_hardware) {
    PoisonToolInfraredRequest request = {
        .operation = PoisonToolInfraredOperationTransmit,
        .timeout_ms = 5000u,
        .maximum_timings = MAX_TIMINGS_AMOUNT,
        .exact_confirmation = false,
    };
    PoisonInfraredResult signal = {
        .decoded = true,
        .protocol = InfraredProtocolNEC,
        .address = 0x10u,
        .command = 0x20u,
        .repeat = false,
    };
    uint8_t evidence[32u];
    size_t evidence_size = 0u;
    char json[POISON_TOOL_INFRARED_RESULT_MAX];
    mu_check(
        poison_tool_infrared_transmit(
            &request,
            PoisonToolInfraredCapabilityTransmit,
            NULL,
            NULL,
            &signal,
            evidence,
            sizeof(evidence),
            &evidence_size,
            json,
            sizeof(json)) == PoisonToolInfraredRunInvalid);

    request.exact_confirmation = true;
    const bool cancelled = true;
    mu_check(
        poison_tool_infrared_transmit(
            &request,
            PoisonToolInfraredCapabilityTransmit,
            poison_tool_infrared_test_cancelled,
            (void*)&cancelled,
            &signal,
            evidence,
            sizeof(evidence),
            &evidence_size,
            json,
            sizeof(json)) == PoisonToolInfraredRunCancelled);
    mu_check(evidence_size == 0u);
    mu_check(!poison_infrared_load_result(NULL, &signal, MAX_TIMINGS_AMOUNT));
    mu_check(!poison_infrared_transmit_once(NULL));
}

MU_TEST(poison_tool_infrared_preserves_decoded_fields) {
    PoisonInfraredResult* result = malloc(sizeof(*result));
    mu_check(result);
    memset(result, 0, sizeof(*result));
    result->decoded = true;
    result->protocol = InfraredProtocolNEC;
    result->address = 0x10u;
    result->command = 0x20u;
    result->repeat = true;
    uint8_t evidence[32u];
    size_t evidence_size = 0u;
    mu_check(
        poison_tool_infrared_evidence_encode(result, evidence, sizeof(evidence), &evidence_size));
    char json[POISON_TOOL_INFRARED_RESULT_MAX];
    mu_check(
        poison_tool_infrared_result_json(result, evidence, evidence_size, json, sizeof(json)));
    mu_check(strstr(json, "\"protocol\":\"NEC\"") != NULL);
    mu_check(strstr(json, "\"address\":16") != NULL);
    mu_check(strstr(json, "\"command\":32") != NULL);
    mu_check(strstr(json, "\"repeat\":true") != NULL);
    memset(result, 0, sizeof(*result));
    free(result);
}

MU_TEST_SUITE(poison_tool_infrared_suite) {
    MU_RUN_TEST(poison_tool_infrared_separates_receive_and_transmit_authority);
    MU_RUN_TEST(poison_tool_infrared_encodes_complete_bounded_evidence);
    MU_RUN_TEST(poison_tool_infrared_preserves_decoded_fields);
    MU_RUN_TEST(poison_tool_infrared_replay_denial_and_cancel_never_touch_hardware);
}

void poison_tool_infrared_run_tests(void) {
    MU_RUN_SUITE(poison_tool_infrared_suite);
}
