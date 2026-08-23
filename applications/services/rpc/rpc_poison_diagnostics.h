#pragma once

#include "rpc_poison_session.h"
#include "../poison_diagnostics/poison_diagnostics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool rpc_poison_diagnostics_snapshot_authenticated(
    PoisonSession* session,
    uint32_t protocol_version,
    uint64_t sequence,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* payload,
    size_t payload_length,
    const uint8_t authentication_tag[POISON_SESSION_AUTH_TAG_BYTES],
    const PoisonDiagnostics* diagnostics,
    PoisonDiagnosticCounters* counters,
    PoisonDiagnosticEvent* events,
    size_t event_capacity,
    size_t* event_count);

#ifdef __cplusplus
}
#endif
