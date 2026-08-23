#include "../test.h"
#include "../../../../services/poison_tools/adapters/poison_tool_ibutton.h"

#include <string.h>

MU_TEST(poison_tool_ibutton_requires_independent_mutation_authority) {
    PoisonToolIButtonRequest request = {
        .operation = PoisonToolIButtonOperationRead,
        .timeout_ms = 5000u,
        .exact_confirmation = false,
    };
    mu_check(poison_tool_ibutton_request_validate(&request, PoisonToolIButtonCapabilityRead));
    mu_check(!poison_tool_ibutton_request_validate(&request, PoisonToolIButtonCapabilityWrite));

    request.operation = PoisonToolIButtonOperationWrite;
    mu_check(!poison_tool_ibutton_request_validate(&request, PoisonToolIButtonCapabilityWrite));
    request.exact_confirmation = true;
    mu_check(poison_tool_ibutton_request_validate(&request, PoisonToolIButtonCapabilityWrite));

    request.operation = PoisonToolIButtonOperationEmulate;
    mu_check(!poison_tool_ibutton_request_validate(&request, PoisonToolIButtonCapabilityWrite));
    mu_check(poison_tool_ibutton_request_validate(&request, PoisonToolIButtonCapabilityEmulate));
}

MU_TEST(poison_tool_ibutton_bounds_wait_and_redacts_rendered_key_data) {
    PoisonToolIButtonRequest request = {
        .operation = PoisonToolIButtonOperationRead,
        .timeout_ms = 0u,
    };
    mu_check(!poison_tool_ibutton_request_validate(&request, PoisonToolIButtonCapabilityRead));
    request.timeout_ms = POISON_TOOL_IBUTTON_TIMEOUT_MAX_MS + 1u;
    mu_check(!poison_tool_ibutton_request_validate(&request, PoisonToolIButtonCapabilityRead));

    PoisonIbuttonReadResult result = {
        .protocol = 1,
        .protocol_name = "DS1990",
        .data = {0x01u, 0x02u, 0xffu},
        .data_size = 3u,
        .valid = true,
        .rendered_size = 8u,
        .rendered = "01 02 FF",
    };
    char first[POISON_TOOL_IBUTTON_RESULT_MAX];
    char second[POISON_TOOL_IBUTTON_RESULT_MAX];
    mu_check(poison_tool_ibutton_result_json(&result, first, sizeof(first)));
    mu_check(poison_tool_ibutton_result_json(&result, second, sizeof(second)));
    mu_assert_string_eq(first, second);
    mu_check(strstr(first, "\"protocol\":\"DS1990\"") != NULL);
    mu_check(strstr(first, "\"data_size\":3") != NULL);
    mu_check(strstr(first, "\"sha256\":\"") != NULL);
    mu_check(strstr(first, "01 02 FF") == NULL);

    result.data_size = sizeof(result.data) + 1u;
    mu_check(!poison_tool_ibutton_result_json(&result, first, sizeof(first)));
    strcpy(result.protocol_name, "bad\"protocol");
    result.data_size = 3u;
    mu_check(!poison_tool_ibutton_result_json(&result, first, sizeof(first)));
}

MU_TEST_SUITE(poison_tool_ibutton_suite) {
    MU_RUN_TEST(poison_tool_ibutton_requires_independent_mutation_authority);
    MU_RUN_TEST(poison_tool_ibutton_bounds_wait_and_redacts_rendered_key_data);
}

void poison_tool_ibutton_run_tests(void) {
    MU_RUN_SUITE(poison_tool_ibutton_suite);
}
