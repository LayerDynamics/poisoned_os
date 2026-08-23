#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "../rpc/poison_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_VFS_PATH_MAX (256u)

typedef enum {
    PoisonVfsNamespaceSystem,
    PoisonVfsNamespaceConfig,
    PoisonVfsNamespaceProfiles,
    PoisonVfsNamespaceApps,
    PoisonVfsNamespaceScripts,
    PoisonVfsNamespaceWorkloads,
    PoisonVfsNamespaceCases,
    PoisonVfsNamespaceEvidence,
    PoisonVfsNamespaceLessons,
    PoisonVfsNamespaceExports,
    PoisonVfsNamespaceInternal,
    PoisonVfsNamespaceExternal,
    PoisonVfsNamespaceCount,
} PoisonVfsNamespace;

typedef enum {
    PoisonVfsOperationRead,
    PoisonVfsOperationWrite
} PoisonVfsOperation;

typedef struct {
    PoisonVfsNamespace vfs_namespace;
    bool read_only;
    char logical_path[POISON_VFS_PATH_MAX + 1u];
    char backing_path[POISON_VFS_PATH_MAX + 1u];
} PoisonVfsResolvedPath;

bool poison_vfs_normalize_path(const char* input, char output[POISON_VFS_PATH_MAX + 1u]);

bool poison_vfs_resolve_path(
    const char* logical_path,
    PoisonRole role,
    PoisonVfsOperation operation,
    PoisonVfsResolvedPath* resolved);

#ifdef __cplusplus
}
#endif
