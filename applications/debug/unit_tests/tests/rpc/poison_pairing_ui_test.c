#include "../../../../settings/poison_security_settings/poison_security_settings.h"
#include "../test.h"

MU_TEST(poison_pairing_ui_requires_exact_physical_confirmation) {
    PoisonSecurityUiState state;
    poison_security_ui_init(&state);
    mu_check(poison_security_ui_begin_pairing(
        &state, "123456", "aa:bb", "workstation", PoisonRoleOperator, 1000));
    mu_check(!poison_security_ui_confirm_pairing(
        &state, "123456", "aa:bb", "workstation", PoisonRoleOperator, 1000));
    mu_check(poison_security_ui_begin_pairing(
        &state, "123456", "aa:bb", "workstation", PoisonRoleOperator, 1000));
    mu_check(!poison_security_ui_confirm_pairing(
        &state, "123456", "aa:cc", "workstation", PoisonRoleOperator, 10));
    mu_check(poison_security_ui_confirm_pairing(
        &state, "123456", "aa:bb", "workstation", PoisonRoleOperator, 10));
    mu_check(!poison_security_ui_confirm_pairing(
        &state, "123456", "aa:bb", "workstation", PoisonRoleOperator, 10));
    mu_check(poison_security_ui_begin_pairing(
        &state, "654321", "cc:dd", "phone", PoisonRoleStudent, 20));
    mu_check(poison_security_ui_cancel_pairing(&state));
    mu_check(!poison_security_ui_pairing_expired(&state, 1));
}

MU_TEST_SUITE(poison_pairing_ui_suite) {
    MU_RUN_TEST(poison_pairing_ui_requires_exact_physical_confirmation);
}

void test_poison_pairing_ui(void) {
    MU_RUN_SUITE(poison_pairing_ui_suite);
}
