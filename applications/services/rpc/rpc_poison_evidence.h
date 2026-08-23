#pragma once

#include "rpc_poison_session.h"
#include "../poison_evidence/poison_evidence.h"

#ifdef __cplusplus
extern "C" {
#endif

bool rpc_poison_evidence_capture_authenticated(
    PoisonSession* session,
    uint32_t protocol_version,
    uint64_t sequence,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* payload,
    size_t payload_length,
    const uint8_t authentication_tag[POISON_SESSION_AUTH_TAG_BYTES],
    PoisonEvidenceStore* store,
    const char* evidence_id,
    const char* case_id,
    bool derived,
    const uint8_t previous_audit_sha256[32]);

#ifdef __cplusplus
}
#endif
