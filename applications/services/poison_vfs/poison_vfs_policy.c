#include "poison_vfs_policy.h"

#include <stdio.h>

bool poison_vfs_policy_allows(
    PoisonRole role,
    PoisonVfsNamespace vfs_namespace,
    PoisonVfsOperation operation) {
    PoisonVfsResolvedPath resolved;
    char path[POISON_VFS_PATH_MAX + 1u];
    const char* names[] = {
        "system",
        "config",
        "profiles",
        "apps",
        "scripts",
        "workloads",
        "cases",
        "evidence",
        "lessons",
        "exports",
        "int",
        "ext"};
    if(vfs_namespace >= PoisonVfsNamespaceCount) return false;
    snprintf(path, sizeof(path), "/%s", names[vfs_namespace]);
    return poison_vfs_resolve_path(path, role, operation, &resolved);
}
