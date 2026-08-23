#pragma once
#include "poison_vfs_paths.h"

bool poison_vfs_policy_allows(
    PoisonRole role,
    PoisonVfsNamespace vfs_namespace,
    PoisonVfsOperation operation);
