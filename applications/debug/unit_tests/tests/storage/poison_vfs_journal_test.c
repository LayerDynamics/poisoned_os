#include "../test.h"
#include "../../../../services/poison_vfs/poison_vfs_recovery.h"

MU_TEST(poison_vfs_journal_recovers_each_commit_boundary) {
    PoisonVfsJournalRecord record;
    uint8_t digest[32] = {0};
    poison_vfs_journal_init(&record);
    mu_check(poison_vfs_journal_prepare(&record, "op-1", "/cases/a", 4, digest, digest));
    mu_check(poison_vfs_journal_validate(&record));
    mu_check(poison_vfs_recover(&record) == PoisonVfsRecoveryRollback);
    mu_check(!record.active);

    poison_vfs_journal_init(&record);
    mu_check(poison_vfs_journal_prepare(&record, "op-2", "/cases/a", 4, digest, digest));
    mu_check(poison_vfs_journal_advance(&record, PoisonVfsJournalDataSynced));
    mu_check(poison_vfs_journal_advance(&record, PoisonVfsJournalMetadataCommitted));
    mu_check(poison_vfs_recover(&record) == PoisonVfsRecoveryComplete);
    mu_check(!record.active);
}

MU_TEST_SUITE(poison_vfs_journal_suite) {
    MU_RUN_TEST(poison_vfs_journal_recovers_each_commit_boundary);
}
void poison_vfs_journal_run_tests(void) {
    MU_RUN_SUITE(poison_vfs_journal_suite);
}
