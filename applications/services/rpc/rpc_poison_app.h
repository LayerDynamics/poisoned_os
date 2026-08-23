#pragma once

#include "rpc_poison_session.h"
#include "../poison_app/poison_app.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RpcSession RpcSession;

bool rpc_poison_app_command_is_bounded(
    const char* app_id,
    const char* command_id,
    const char* payload_json);

bool rpc_poison_app_accept_event_authenticated(
    PoisonSession* session,
    uint32_t protocol_version,
    uint64_t sequence,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* payload,
    size_t payload_length,
    const uint8_t authentication_tag[POISON_SESSION_AUTH_TAG_BYTES],
    PoisonAppRun* run,
    const PoisonAppEvent* event);

#ifdef __cplusplus
}
#endif
