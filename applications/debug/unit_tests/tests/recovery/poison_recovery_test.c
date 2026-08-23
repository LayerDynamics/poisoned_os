#include "../test.h"
#include "../../../../system/poison_recovery/poison_recovery.h"
#include "../../../../services/poison_diagnostics/poison_diagnostics.h"

MU_TEST(poison_recovery_preserves_data_and_requires_known_good) {
    PoisonRecovery recovery;
    poison_diagnostics_init(poison_diagnostics_get());
    poison_recovery_boot_init(&recovery, true, true);
    mu_check(recovery.preserve_user_data);
    mu_check(poison_recovery_enter_menu(&recovery));
    mu_check(poison_recovery_begin_firmware(&recovery));
    mu_check(poison_recovery_complete(&recovery, true));
    mu_check(recovery.state == PoisonRecoveryComplete);
    mu_check(poison_diagnostics_counters(poison_diagnostics_get())->recoveries == 1u);
    poison_recovery_boot_init(&recovery, false, false);
    mu_check(poison_recovery_enter_menu(&recovery));
    mu_check(!poison_recovery_begin_reindex(&recovery));
    mu_check(poison_diagnostics_counters(poison_diagnostics_get())->recoveries == 1u);
}

MU_TEST(poison_recovery_cancel_is_terminal) {
    PoisonRecovery recovery;
    poison_recovery_boot_init(&recovery, false, true);
    mu_check(poison_recovery_enter_menu(&recovery));
    mu_check(poison_recovery_begin_profile(&recovery));
    mu_check(poison_recovery_cancel(&recovery));
    mu_check(!poison_recovery_complete(&recovery, true));
}

MU_TEST_SUITE(poison_recovery_suite) {
    MU_RUN_TEST(poison_recovery_preserves_data_and_requires_known_good);
    MU_RUN_TEST(poison_recovery_cancel_is_terminal);
}
void poison_recovery_run_tests(void) {
    MU_RUN_SUITE(poison_recovery_suite);
}
