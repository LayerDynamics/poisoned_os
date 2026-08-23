#pragma once

#include "poison_audit.h"

size_t poison_audit_snapshot(
    const PoisonAuditChain* chain,
    uint64_t after_event_id,
    PoisonAuditEvent* events,
    size_t event_capacity,
    bool* truncated,
    uint64_t* next_event_id,
    uint8_t last_digest[POISON_AUDIT_DIGEST_BYTES]);
