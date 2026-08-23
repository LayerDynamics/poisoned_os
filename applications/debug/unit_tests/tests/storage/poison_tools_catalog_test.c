#include "../test.h"
#include "../../../../services/poison_tools/poison_tools.h"
#include "../../../../services/poison_tools/poison_tools_i.h"
#include "../../../../services/poison_tools/poison_tool_adapters.h"
#include "../../../../services/poison_app/poison_app_i.h"
#include <flipper.pb.h>

extern bool rpc_poison_tool_run_request_is_valid(const PB_Poison_ToolRun* input);

MU_TEST(poison_tools_require_verified_adapter_before_execution) {
    PoisonToolDescriptor descriptor = {
        .id = "nfc.read",
        .family = "nfc",
        .capability = "nfc.read",
        .status = PoisonToolCatalogFoundation,
        .adapter_present = false};
    mu_check(poison_tool_descriptor_valid(&descriptor));
    mu_check(!poison_tool_can_execute(&descriptor, "nfc.read"));
    descriptor.status = PoisonToolCatalogVerified;
    descriptor.adapter_present = true;
    mu_check(poison_tool_can_execute(&descriptor, "nfc.read"));
    mu_check(!poison_tool_can_execute(&descriptor, "nfc.write"));
}

MU_TEST(poison_tools_route_specialized_tools_before_family_fallback) {
    PoisonToolDescriptor marauder = {
        .id = "marauder.console",
        .family = "serial",
        .capability = "marauder.observe",
        .status = PoisonToolCatalogVerified,
        .adapter_present = true};
    mu_check(poison_tool_can_execute(&marauder, "marauder.observe"));
    mu_check(!poison_tool_can_execute(&marauder, "serial.observe"));

    PoisonToolDescriptor esp_flasher = {
        .id = "esp-flasher",
        .family = "gpio",
        .capability = "esp.flash",
        .status = PoisonToolCatalogVerified,
        .adapter_present = true};
    mu_check(poison_tool_can_execute(&esp_flasher, "esp.flash"));
    mu_check(!poison_tool_can_execute(&esp_flasher, "gpio.write"));
}

MU_TEST(poison_tools_catalog_maps_supported_hardware_families) {
    const char* families[] = {
        "nfc",
        "lf-rfid",
        "ibutton",
        "infrared",
        "sub-ghz",
        "gpio",
        "usb-hid",
        "ble-hid",
        "serial",
        "storage",
    };
    for(size_t i = 0; i < sizeof(families) / sizeof(families[0]); i++) {
        PoisonToolAdapter adapter;
        mu_check(poison_tool_adapter_for_family(families[i], &adapter));
    }
}

MU_TEST(poison_tools_catalog_exposes_driver_operations);
MU_TEST(poison_tools_catalog_has_real_run_definitions_and_role_gates);
MU_TEST(poison_tools_rpc_request_contract_is_bounded);
MU_TEST(poison_tools_status_run_dispatches_through_structured_app);
MU_TEST(poison_tools_json_results_escape_device_text);
MU_TEST(poison_tools_cancel_interrupts_queued_hardware_wait);

MU_TEST_SUITE(poison_tools_catalog_suite) {
    MU_RUN_TEST(poison_tools_require_verified_adapter_before_execution);
    MU_RUN_TEST(poison_tools_route_specialized_tools_before_family_fallback);
    MU_RUN_TEST(poison_tools_catalog_maps_supported_hardware_families);
    MU_RUN_TEST(poison_tools_catalog_exposes_driver_operations);
    MU_RUN_TEST(poison_tools_catalog_has_real_run_definitions_and_role_gates);
    MU_RUN_TEST(poison_tools_rpc_request_contract_is_bounded);
    MU_RUN_TEST(poison_tools_status_run_dispatches_through_structured_app);
    MU_RUN_TEST(poison_tools_json_results_escape_device_text);
    MU_RUN_TEST(poison_tools_cancel_interrupts_queued_hardware_wait);
}

MU_TEST(poison_tools_catalog_exposes_driver_operations) {
    PoisonToolAdapter adapter;
    mu_check(poison_tool_adapter_for_family("ibutton", &adapter));
    mu_check(poison_tool_adapter_supports_capability(adapter, "ibutton.read"));
    mu_check(poison_tool_adapter_supports_capability(adapter, "ibutton.emulate"));
    mu_check(poison_tool_adapter_for_family("sub-ghz", &adapter));
    mu_check(poison_tool_adapter_supports_capability(adapter, "sub-ghz.transmit"));
    mu_check(poison_tool_adapter_for_family("ble-hid", &adapter));
    mu_check(poison_tool_adapter_supports_capability(adapter, "ble.mouse"));
    mu_check(poison_tool_adapter_for_family("nfc", &adapter));
    mu_check(poison_tool_adapter_supports_capability(adapter, "nfc.read"));
    mu_check(poison_tool_adapter_for_family("lf-rfid", &adapter));
    mu_check(poison_tool_adapter_supports_capability(adapter, "lf-rfid.read"));
    mu_check(poison_tool_adapter_for_family("serial", &adapter));
    mu_check(poison_tool_adapter_supports_capability(adapter, "serial.observe"));
    mu_check(poison_tool_adapter_for_family("storage", &adapter));
    mu_check(poison_tool_adapter_supports_capability(adapter, "storage.read"));
}

MU_TEST(poison_tools_catalog_has_real_run_definitions_and_role_gates) {
    static const char* const tools[] = {
        "nfc.read",
        "lf-rfid.read",
        "ibutton.read",
        "infrared.receive",
        "sub-ghz.receive",
        "gpio.inspect",
        "usb-hid.inspect",
        "ble-hid.status",
        "serial.observe",
        "storage.inspect",
        "marauder.console",
    };
    for(size_t index = 0u; index < sizeof(tools) / sizeof(tools[0]); index++) {
        const PoisonToolDefinition* definition = poison_tool_definition_find(tools[index]);
        mu_check(definition != NULL);
        mu_check(poison_tool_definition_authorized(definition, PoisonRoleOwner));
    }
    mu_check(poison_tool_definition_authorized(
        poison_tool_definition_find("usb-hid.inspect"), PoisonRoleObserver));
    mu_check(!poison_tool_definition_authorized(
        poison_tool_definition_find("nfc.read"), PoisonRoleObserver));
    mu_check(!poison_tool_definition_authorized(
        poison_tool_definition_find("storage.inspect"), PoisonRoleStudent));
    mu_check(poison_tool_definition_find("missing.tool") == NULL);
    mu_check(poison_tool_adapter_for_tool("marauder.console", "serial", &(PoisonToolAdapter){0}));
}

MU_TEST(poison_tools_rpc_request_contract_is_bounded) {
    PB_Poison_ToolRun request = PB_Poison_ToolRun_init_zero;
    strcpy(request.tool_id, "usb-hid.inspect");
    strcpy(request.run_id, "run-contract");
    strcpy(request.case_id, "case-contract");
    strcpy(request.tool_version, "builtin");
    strcpy(request.state, "start");
    mu_check(rpc_poison_tool_run_request_is_valid(&request));
    strcpy(request.tool_version, "host-development");
    mu_check(!rpc_poison_tool_run_request_is_valid(&request));
    strcpy(request.tool_version, "builtin");
    strcpy(request.state, "pause");
    mu_check(!rpc_poison_tool_run_request_is_valid(&request));
    request.case_id[0] = '\0';
    mu_check(!rpc_poison_tool_run_request_is_valid(&request));
}

MU_TEST(poison_tools_status_run_dispatches_through_structured_app) {
    const char* run_id = "run-usb-status";
    mu_check(poison_tools_run_start("usb-hid.inspect", run_id, PoisonRoleObserver));
    mu_check(poison_tools_run_is_active(run_id));
    PoisonAppCommand command = {
        .protocol_version = POISON_APP_PROTOCOL_VERSION,
        .app_id = "org.poison.tools",
        .run_id = run_id,
        .command_id = "usb-hid.status",
        .payload_json = "{}",
        .cancel = false,
    };
    mu_check(poison_app_dispatch_command(&command));
    mu_check(poison_tools_run_stop(run_id));
    mu_check(!poison_tools_run_is_active(run_id));
}

MU_TEST(poison_tools_json_results_escape_device_text) {
    char output[64];
    mu_check(poison_tools_json_escape_string("a\"b\\c\n\t", output, sizeof(output)));
    mu_assert_string_eq("a\\\"b\\\\c\\n\\t", output);

    const char non_ascii[] = {(char)0x80, '\0'};
    mu_check(poison_tools_json_escape_string(non_ascii, output, sizeof(output)));
    mu_assert_string_eq("\\u0080", output);

    char too_small[2];
    mu_check(!poison_tools_json_escape_string("\"", too_small, sizeof(too_small)));
    mu_check(!poison_tools_json_escape_string(NULL, output, sizeof(output)));
}

MU_TEST(poison_tools_cancel_interrupts_queued_hardware_wait) {
    const char* run_id = "run-cancel-nfc";
    mu_check(poison_tools_run_start("nfc.read", run_id, PoisonRoleOwner));
    PoisonAppCommand command = {
        .protocol_version = POISON_APP_PROTOCOL_VERSION,
        .app_id = "org.poison.tools",
        .run_id = run_id,
        .command_id = "nfc.detect",
        .payload_json = "{\"timeout_ms\":5000}",
        .cancel = false,
    };
    mu_check(poison_app_dispatch_command(&command));
    command.cancel = true;
    mu_check(poison_app_dispatch_command(&command));
    mu_check(poison_tools_run_stop(run_id));
    mu_check(!poison_tools_run_is_active(run_id));
}
void poison_tools_catalog_run_tests(void) {
    MU_RUN_SUITE(poison_tools_catalog_suite);
}
