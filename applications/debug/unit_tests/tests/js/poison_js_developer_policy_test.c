#include "../test.h"
#include "../../../../system/js_app/js_developer_policy.h"
MU_TEST(poison_js_developer_policy_binds_and_expires) {
    JsDeveloperPolicy policy;
    js_developer_policy_init(&policy);
    mu_check(
        !js_developer_policy_activate(&policy, "signer", "digest", "gpio.read", 100, 10, false));
    mu_check(
        js_developer_policy_activate(&policy, "signer", "digest", "gpio.read", 100, 10, true));
    mu_check(js_developer_policy_allows(&policy, "signer", "digest", "gpio.read", 105));
    mu_check(!js_developer_policy_allows(&policy, "signer", "digest", "pio", 105));
    mu_check(!js_developer_policy_allows(&policy, "signer", "digest", "gpio", 105));
    mu_check(!js_developer_policy_allows(&policy, "other", "digest", "gpio.read", 105));
    mu_check(js_developer_policy_expire(&policy, 110));
    mu_check(!js_developer_policy_allows(&policy, "signer", "digest", "gpio.read", 110));
}
MU_TEST(poison_js_developer_policy_revokes) {
    JsDeveloperPolicy policy;
    js_developer_policy_init(&policy);
    mu_check(
        js_developer_policy_activate(&policy, "signer", "digest", "serial.read", 0, 100, true));
    mu_check(js_developer_policy_revoke(&policy));
    mu_check(!js_developer_policy_allows(&policy, "signer", "digest", "serial.read", 1));
    mu_check(
        !js_developer_policy_activate(&policy, "signer", "digest", "serial.read", 2, 100, true));
}
MU_TEST_SUITE(poison_js_developer_policy_suite) {
    MU_RUN_TEST(poison_js_developer_policy_binds_and_expires);
    MU_RUN_TEST(poison_js_developer_policy_revokes);
}
void poison_js_developer_policy_run_tests(void) {
    MU_RUN_SUITE(poison_js_developer_policy_suite);
}
