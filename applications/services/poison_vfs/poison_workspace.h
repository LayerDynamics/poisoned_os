#pragma once

#include <stdbool.h>

typedef struct {
    bool active;
    char snapshot_id[65];
    char workspace_path[257];
} PoisonWorkspaceSnapshot;

bool poison_workspace_snapshot_create(
    PoisonWorkspaceSnapshot* snapshot,
    const char* snapshot_id,
    const char* workspace_path);
bool poison_workspace_reset_preview(
    const PoisonWorkspaceSnapshot* snapshot,
    const char* workspace_path);
bool poison_workspace_reset_confirm(
    PoisonWorkspaceSnapshot* snapshot,
    const char* workspace_path,
    bool exact_confirmation);
