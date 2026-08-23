#pragma once

#include "rpc_poison_session.h"
#include "../poison_vfs/poison_workspace.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool rpc_poison_workspace_snapshot_create_authenticated(
    PoisonSession* session,
    uint32_t protocol_version,
    uint64_t sequence,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* payload,
    size_t payload_length,
    const uint8_t authentication_tag[POISON_SESSION_AUTH_TAG_BYTES],
    PoisonWorkspaceSnapshot* snapshot,
    const char* snapshot_id,
    const char* workspace_path);

bool rpc_poison_workspace_reset_confirm_authenticated(
    PoisonSession* session,
    uint32_t protocol_version,
    uint64_t sequence,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* payload,
    size_t payload_length,
    const uint8_t authentication_tag[POISON_SESSION_AUTH_TAG_BYTES],
    PoisonWorkspaceSnapshot* snapshot,
    const char* workspace_path,
    bool exact_confirmation);

#ifdef __cplusplus
}
#endif
