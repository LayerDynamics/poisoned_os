#include "../test.h"
#include "../../../../services/poison_tools/adapters/poison_tool_lfrfid.h"

#include <string.h>

MU_TEST(poison_tool_lfrfid_separates_read_write_and_emulation_authority) {
    PoisonToolLfRfidRequest request = {
        .operation = PoisonToolLfRfidOperationRead,
        .timeout_ms = 5000u,
        .exact_confirmation = false,
    };
    mu_check(poison_tool_lfrfid_request_validate(&request, PoisonToolLfRfidCapabilityRead));
    mu_check(!poison_tool_lfrfid_request_validate(&request, PoisonToolLfRfidCapabilityWrite));

    request.operation = PoisonToolLfRfidOperationWrite;
    mu_check(!poison_tool_lfrfid_request_validate(&request, PoisonToolLfRfidCapabilityWrite));
    request.exact_confirmation = true;
    mu_check(poison_tool_lfrfid_request_validate(&request, PoisonToolLfRfidCapabilityWrite));

    request.operation = PoisonToolLfRfidOperationEmulate;
    mu_check(!poison_tool_lfrfid_request_validate(&request, PoisonToolLfRfidCapabilityWrite));
    mu_check(poison_tool_lfrfid_request_validate(&request, PoisonToolLfRfidCapabilityEmulate));
}

MU_TEST(poison_tool_lfrfid_redacts_credential_bytes_from_structured_results) {
    PoisonLfRfidDetection detection = {
        .protocol = "EM4100",
        .data = {0x01u, 0x02u, 0xffu},
        .data_size = 3u,
    };
    char first[POISON_TOOL_LFRFID_RESULT_MAX];
    char second[POISON_TOOL_LFRFID_RESULT_MAX];
    mu_check(poison_tool_lfrfid_detection_json(&detection, first, sizeof(first)));
    mu_check(poison_tool_lfrfid_detection_json(&detection, second, sizeof(second)));
    mu_assert_string_eq(first, second);
    mu_check(strstr(first, "\"protocol\":\"EM4100\"") != NULL);
    mu_check(strstr(first, "\"data_size\":3") != NULL);
    mu_check(strstr(first, "\"sha256\":\"") != NULL);
    mu_check(strstr(first, "0102ff") == NULL);

    detection.data_size = 0u;
    mu_check(!poison_tool_lfrfid_detection_json(&detection, first, sizeof(first)));
    detection.data_size = POISON_LFRFID_DATA_MAX + 1u;
    mu_check(!poison_tool_lfrfid_detection_json(&detection, first, sizeof(first)));
}

MU_TEST_SUITE(poison_tool_lfrfid_suite) {
    MU_RUN_TEST(poison_tool_lfrfid_separates_read_write_and_emulation_authority);
    MU_RUN_TEST(poison_tool_lfrfid_redacts_credential_bytes_from_structured_results);
}

void poison_tool_lfrfid_run_tests(void) {
    MU_RUN_SUITE(poison_tool_lfrfid_suite);
}
