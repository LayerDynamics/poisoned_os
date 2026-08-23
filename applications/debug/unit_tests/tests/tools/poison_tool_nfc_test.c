#include "../test.h"
#include "../../../../services/poison_tools/adapters/poison_tool_nfc.h"

MU_TEST(poison_tool_nfc_requires_separate_capabilities_and_exact_confirmation) {
    PoisonToolNfcRequest request = {
        .operation = PoisonToolNfcOperationDetect,
        .timeout_ms = 5000u,
        .maximum_capture_bytes = 0u,
        .exact_confirmation = false,
    };

    mu_check(poison_tool_nfc_request_validate(&request, PoisonToolNfcCapabilityRead));
    mu_check(!poison_tool_nfc_request_validate(&request, PoisonToolNfcCapabilityRawCapture));

    request.operation = PoisonToolNfcOperationRawCapture;
    request.maximum_capture_bytes = POISON_TOOL_NFC_RAW_CAPTURE_MAX;
    mu_check(poison_tool_nfc_request_validate(&request, PoisonToolNfcCapabilityRawCapture));
    request.maximum_capture_bytes = POISON_TOOL_NFC_RAW_CAPTURE_MAX + 1u;
    mu_check(!poison_tool_nfc_request_validate(&request, PoisonToolNfcCapabilityRawCapture));

    request.operation = PoisonToolNfcOperationWrite;
    request.maximum_capture_bytes = 8u;
    mu_check(!poison_tool_nfc_request_validate(&request, PoisonToolNfcCapabilityWrite));
    request.exact_confirmation = true;
    mu_check(poison_tool_nfc_request_validate(&request, PoisonToolNfcCapabilityWrite));

    request.operation = PoisonToolNfcOperationEmulate;
    mu_check(!poison_tool_nfc_request_validate(&request, PoisonToolNfcCapabilityWrite));
    mu_check(poison_tool_nfc_request_validate(&request, PoisonToolNfcCapabilityEmulate));
}

MU_TEST(poison_tool_nfc_bounds_time_and_serializes_detection) {
    PoisonToolNfcRequest request = {
        .operation = PoisonToolNfcOperationDetect,
        .timeout_ms = 0u,
    };
    mu_check(!poison_tool_nfc_request_validate(&request, PoisonToolNfcCapabilityRead));
    request.timeout_ms = POISON_TOOL_NFC_TIMEOUT_MAX_MS + 1u;
    mu_check(!poison_tool_nfc_request_validate(&request, PoisonToolNfcCapabilityRead));

    PoisonNfcDetection detection = {
        .protocols = {NfcProtocolIso14443_3a, NfcProtocolMfClassic},
        .protocol_count = 2u,
    };
    char json[POISON_TOOL_NFC_RESULT_MAX];
    mu_check(poison_tool_nfc_detection_json(&detection, json, sizeof(json)));
    mu_assert_string_eq("{\"protocols\":[\"iso14443-3a\",\"mifare-classic\"]}", json);

    detection.protocol_count = NfcProtocolNum + 1u;
    mu_check(!poison_tool_nfc_detection_json(&detection, json, sizeof(json)));
    mu_check(!poison_tool_nfc_detection_json(&detection, json, 8u));
}

MU_TEST_SUITE(poison_tool_nfc_suite) {
    MU_RUN_TEST(poison_tool_nfc_requires_separate_capabilities_and_exact_confirmation);
    MU_RUN_TEST(poison_tool_nfc_bounds_time_and_serializes_detection);
}

void poison_tool_nfc_run_tests(void) {
    MU_RUN_SUITE(poison_tool_nfc_suite);
}
