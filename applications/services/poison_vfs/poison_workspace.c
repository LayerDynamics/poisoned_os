#include "poison_workspace.h"

#include "poison_vfs_paths.h"

#include <string.h>

bool poison_workspace_snapshot_create(
    PoisonWorkspaceSnapshot* snapshot,
    const char* snapshot_id,
    const char* workspace_path) {
    if(!snapshot || !snapshot_id || !workspace_path || snapshot->active ||
       snapshot_id[0] == '\0' || strnlen(snapshot_id, 65) >= 65)
        return false;
    char normalized[POISON_VFS_PATH_MAX + 1u];
    if(!poison_vfs_normalize_path(workspace_path, normalized) ||
       strncmp(normalized, "/cases/", 7) != 0)
        return false;
    memset(snapshot, 0, sizeof(*snapshot));
    strcpy(snapshot->snapshot_id, snapshot_id);
    strcpy(snapshot->workspace_path, normalized);
    snapshot->active = true;
    return true;
}

bool poison_workspace_reset_preview(
    const PoisonWorkspaceSnapshot* snapshot,
    const char* workspace_path) {
    if(!snapshot || !snapshot->active || !workspace_path) return false;
    char normalized[POISON_VFS_PATH_MAX + 1u];
    return poison_vfs_normalize_path(workspace_path, normalized) &&
           strcmp(normalized, snapshot->workspace_path) == 0;
}

bool poison_workspace_reset_confirm(
    PoisonWorkspaceSnapshot* snapshot,
    const char* workspace_path,
    bool exact_confirmation) {
    if(!exact_confirmation || !poison_workspace_reset_preview(snapshot, workspace_path))
        return false;
    snapshot->active = false;
    return true;
}
