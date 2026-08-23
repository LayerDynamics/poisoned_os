#include "rpc_poison_workspace.h"

#include <string.h>

static bool authenticate_workspace_frame(
    PoisonSession* session,
    uint32_t protocol_version,
    uint64_t sequence,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* payload,
    size_t payload_length,
    const uint8_t authentication_tag[POISON_SESSION_AUTH_TAG_BYTES]) {
    return poison_session_authenticate_rx(
               session,
               protocol_version,
               sequence,
               acknowledgement,
               channel,
               payload,
               payload_length,
               authentication_tag) == PoisonSessionResultOk;
}

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
    const char* workspace_path) {
    if(!channel || strcmp(channel, "workspace") != 0) return false;
    if(!authenticate_workspace_frame(
           session,
           protocol_version,
           sequence,
           acknowledgement,
           channel,
           payload,
           payload_length,
           authentication_tag)) {
        return false;
    }
    return poison_workspace_snapshot_create(snapshot, snapshot_id, workspace_path);
}

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
    bool exact_confirmation) {
    if(!channel || strcmp(channel, "workspace") != 0) return false;
    if(!authenticate_workspace_frame(
           session,
           protocol_version,
           sequence,
           acknowledgement,
           channel,
           payload,
           payload_length,
           authentication_tag)) {
        return false;
    }
    return poison_workspace_reset_confirm(snapshot, workspace_path, exact_confirmation);
}
