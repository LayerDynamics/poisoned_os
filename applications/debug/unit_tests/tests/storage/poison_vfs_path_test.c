#include "../test.h"
#include "../../../../services/poison_vfs/poison_vfs_paths.h"

MU_TEST(poison_vfs_rejects_traversal_and_normalizes_once) {
    char normalized[POISON_VFS_PATH_MAX + 1u];
    mu_check(poison_vfs_normalize_path("//evidence///case", normalized));
    mu_assert_string_eq("/evidence/case", normalized);
    mu_check(!poison_vfs_normalize_path("/evidence/../config", normalized));
    mu_check(!poison_vfs_normalize_path("/evidence\\case", normalized));
}

MU_TEST(poison_vfs_maps_namespaces_and_enforces_roles) {
    PoisonVfsResolvedPath resolved;
    mu_check(poison_vfs_resolve_path(
        "/evidence/case/raw.bin", PoisonRoleOperator, PoisonVfsOperationWrite, &resolved));
    mu_assert_string_eq("/ext/evidence/case/raw.bin", resolved.backing_path);
    mu_check(!poison_vfs_resolve_path(
        "/system/firmware", PoisonRoleOperator, PoisonVfsOperationWrite, &resolved));
    mu_check(poison_vfs_resolve_path(
        "/system/firmware", PoisonRoleObserver, PoisonVfsOperationRead, &resolved));
    mu_check(!poison_vfs_resolve_path(
        "/evidence/case", PoisonRoleObserver, PoisonVfsOperationWrite, &resolved));
}

MU_TEST_SUITE(poison_vfs_path_suite) {
    MU_RUN_TEST(poison_vfs_rejects_traversal_and_normalizes_once);
    MU_RUN_TEST(poison_vfs_maps_namespaces_and_enforces_roles);
}

void poison_vfs_path_run_tests(void) {
    MU_RUN_SUITE(poison_vfs_path_suite);
}
