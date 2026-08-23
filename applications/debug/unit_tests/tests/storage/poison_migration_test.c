#include "../test.h"
#include "../../../../services/poison_vfs/poison_migration.h"

MU_TEST(poison_migration_requires_verified_backup_and_space) {
    PoisonMigration migration;
    uint8_t digest[32] = {0};
    poison_migration_init(&migration);
    mu_check(!poison_migration_add(
        &migration,
        "/ext/capture.sub",
        "/captures/capture.sub",
        "legacy/capture.sub",
        10,
        10,
        10,
        digest,
        digest,
        false,
        PoisonMigrationConvertible));
    mu_check(poison_migration_add(
        &migration,
        "/ext/capture.sub",
        "/captures/capture.sub",
        "legacy/capture.sub",
        10,
        10,
        10,
        digest,
        digest,
        true,
        PoisonMigrationConvertible));
    mu_check(!poison_migration_verify(&migration, 9));
    mu_check(poison_migration_verify(&migration, 10));
    mu_check(poison_migration_commit(&migration));
    mu_check(poison_migration_rollback(&migration));
}

MU_TEST(poison_migration_rejects_unsafe_paths_and_duplicates) {
    PoisonMigration migration;
    uint8_t digest[32] = {0};
    poison_migration_init(&migration);
    mu_check(!poison_migration_add(
        &migration,
        "/tmp/escape",
        "/captures/a",
        "backup/a",
        1,
        0,
        1,
        digest,
        digest,
        true,
        PoisonMigrationUnknown));
    mu_check(!poison_migration_add(
        &migration,
        "/ext/../escape",
        "/captures/a",
        "backup/a",
        1,
        0,
        1,
        digest,
        digest,
        true,
        PoisonMigrationUnknown));
    mu_check(poison_migration_add(
        &migration,
        "/int/legacy",
        "/unknown/legacy",
        "backup/a",
        1,
        0,
        1,
        digest,
        digest,
        true,
        PoisonMigrationUnknown));
    mu_check(!poison_migration_add(
        &migration,
        "/int/legacy",
        "/unknown/other",
        "backup/b",
        1,
        0,
        1,
        digest,
        digest,
        true,
        PoisonMigrationUnknown));
}

MU_TEST_SUITE(poison_migration_suite) {
    MU_RUN_TEST(poison_migration_requires_verified_backup_and_space);
    MU_RUN_TEST(poison_migration_rejects_unsafe_paths_and_duplicates);
}
void poison_migration_run_tests(void) {
    MU_RUN_SUITE(poison_migration_suite);
}
