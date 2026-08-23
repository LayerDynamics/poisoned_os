#include "rpc_poison_diagnostics.h"

#include <string.h>

static size_t diagnostic_event_count(const PoisonDiagnostics* diagnostics) {
    size_t count = 0;
    for(size_t index = 0; index < POISON_DIAGNOSTICS_RING_SIZE; ++index) {
        if(diagnostics->events[index].event_id != 0u) ++count;
    }
    return count;
}

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
    size_t* event_count) {
    if(!diagnostics || !counters || !events || !event_count || !channel ||
       strcmp(channel, "diagnostics") != 0) {
        return false;
    }
    const size_t count = diagnostic_event_count(diagnostics);
    if(event_capacity < count) return false;
    if(poison_session_authenticate_rx(
           session,
           protocol_version,
           sequence,
           acknowledgement,
           channel,
           payload,
           payload_length,
           authentication_tag) != PoisonSessionResultOk) {
        return false;
    }
    *counters = diagnostics->counters;
    size_t copied = 0;
    for(size_t index = 0; index < POISON_DIAGNOSTICS_RING_SIZE; ++index) {
        if(diagnostics->events[index].event_id == 0u) continue;
        events[copied++] = diagnostics->events[index];
    }
    for(size_t index = 1; index < copied; ++index) {
        PoisonDiagnosticEvent value = events[index];
        size_t insertion = index;
        while(insertion > 0 && events[insertion - 1u].event_id > value.event_id) {
            events[insertion] = events[insertion - 1u];
            --insertion;
        }
        events[insertion] = value;
    }
    *event_count = copied;
    return true;
}
