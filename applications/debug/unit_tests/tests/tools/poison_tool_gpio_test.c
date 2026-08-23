#include <furi.h>
#include "../test.h"
#include <string.h>
#include "../../../../services/poison_tools/poison_gpio_adapter.h"
#include "../../../../services/poison_tools/poison_esp_adapter.h"
#include "../../../../services/poison_tools/poison_infrared_adapter.h"
#include "../../../../services/poison_tools/poison_marauder_adapter.h"
#include "../../../../services/poison_tools/poison_usb_hid_adapter.h"
#include "../../../../services/poison_tools/poison_ble_adapter.h"
#include <applications/drivers/esp32marauder/esp32_marauder_driver.h>

MU_TEST(poison_gpio_pin_names_and_parse) {
    PoisonGpioPin pin;
    mu_check(poison_gpio_pin_parse("pc3", &pin));
    mu_check(pin == PoisonGpioPinPc3);
    mu_check(strcmp("PC3", poison_gpio_pin_name(pin)) == 0);
    mu_check(!poison_gpio_pin_parse("display", &pin));
    mu_check(poison_gpio_pin_name((PoisonGpioPin)99) == NULL);
}

MU_TEST(poison_gpio_bounds_are_rejected) {
    PoisonGpioSample sample;
    mu_check(!poison_gpio_sample(PoisonGpioPinPc0, 0u, 10u, &sample));
    mu_check(!poison_gpio_sample(PoisonGpioPinPc0, 1u, 60001u, &sample));
}

MU_TEST(poison_esp_target_matrix_matches_bundled_firmware) {
    static const char* const targets[] = {
        "flipper-zero-wifi-dev-board",
        "marauder-s2",
        "marauder-s3",
        "flipperhttp-s2",
        "wardriver-wroom",
        "wardriver-s3",
    };
    for(size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
        PoisonEspTarget target;
        mu_check(poison_esp_target_parse(targets[i], &target));
        mu_check(strcmp(targets[i], poison_esp_target_id(target)) == 0);
    }
    PoisonEspTarget invalid;
    mu_check(!poison_esp_target_parse("display-link", &invalid));
    mu_check(poison_esp_target_id((PoisonEspTarget)99) == NULL);
}

MU_TEST(poison_marauder_driver_exposes_bounded_momentum_command_coverage) {
    mu_check(esp32_marauder_command_count() >= 90u);
    const Esp32MarauderCommandDescriptor* capture = esp32_marauder_command_find("sniff.pmkid");
    mu_check(capture != NULL);
    mu_check(capture->produces_capture);
    mu_check(capture->capability == Esp32MarauderCapabilityControl);
    const Esp32MarauderCommandDescriptor* attack = esp32_marauder_command_find("attack.deauth");
    mu_check(attack != NULL);
    mu_check(attack->capability == Esp32MarauderCapabilityActive);

    char command[ESP32_MARAUDER_COMMAND_MAX];
    mu_check(esp32_marauder_command_format("list.ap", NULL, command, sizeof(command)));
    mu_check(strcmp(command, "list -a\n") == 0);
    mu_check(esp32_marauder_command_format("channel.set", "11", command, sizeof(command)));
    mu_check(strcmp(command, "channel -s 11\n") == 0);
    mu_check(
        !esp32_marauder_command_format("channel.set", "11\nreboot", command, sizeof(command)));
    mu_check(
        !esp32_marauder_command_format("attack.deauth", "unexpected", command, sizeof(command)));
    mu_check(esp32_marauder_command_at(esp32_marauder_command_count()) == NULL);
}

MU_TEST(poison_infrared_rejects_invalid_lifecycle_inputs) {
    PoisonInfraredResult result;
    mu_check(!poison_infrared_receive(NULL, 100u, &result));
    mu_check(!poison_infrared_receive(NULL, 0u, &result));
    mu_check(!poison_infrared_transmit(NULL));
}

MU_TEST(poison_marauder_rejects_invalid_overflow_query) {
    mu_check(poison_marauder_session_take_dropped(NULL) == 0u);
}

MU_TEST(poison_usb_ble_reject_invalid_handles) {
    mu_check(!poison_ble_start_advertising(NULL));
    poison_ble_stop_advertising(NULL);
    poison_usb_hid_release_all();
}

MU_TEST_SUITE(poison_tool_gpio_suite) {
    MU_RUN_TEST(poison_gpio_pin_names_and_parse);
    MU_RUN_TEST(poison_gpio_bounds_are_rejected);
    MU_RUN_TEST(poison_esp_target_matrix_matches_bundled_firmware);
    MU_RUN_TEST(poison_marauder_driver_exposes_bounded_momentum_command_coverage);
    MU_RUN_TEST(poison_infrared_rejects_invalid_lifecycle_inputs);
    MU_RUN_TEST(poison_marauder_rejects_invalid_overflow_query);
    MU_RUN_TEST(poison_usb_ble_reject_invalid_handles);
}

void poison_tool_gpio_run_tests(void) {
    MU_RUN_SUITE(poison_tool_gpio_suite);
}
