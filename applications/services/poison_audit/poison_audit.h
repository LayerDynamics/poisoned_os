#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_AUDIT_DIGEST_BYTES (32u)
#define POISON_AUDIT_TEXT_MAX     (32u)
#define POISON_AUDIT_METADATA_MAX (160u)
#define POISON_AUDIT_RING_SIZE    (16u)

typedef enum {
    PoisonAuditDecisionDenied,
    PoisonAuditDecisionAllowed,
    PoisonAuditDecisionExpired,
    PoisonAuditDecisionRevoked,
} PoisonAuditDecision;

typedef struct {
    uint64_t event_id;
    uint8_t prior_digest[POISON_AUDIT_DIGEST_BYTES];
    uint8_t actor_digest[POISON_AUDIT_DIGEST_BYTES];
    char action[POISON_AUDIT_TEXT_MAX + 1u];
    char resource[POISON_AUDIT_TEXT_MAX + 1u];
    PoisonAuditDecision decision;
    uint64_t timestamp_ms;
    uint8_t correlation_id[POISON_AUDIT_DIGEST_BYTES];
    char safe_metadata[POISON_AUDIT_METADATA_MAX + 1u];
    uint8_t digest[POISON_AUDIT_DIGEST_BYTES];
} PoisonAuditEvent;

typedef struct {
    uint64_t next_event_id;
    uint8_t last_digest[POISON_AUDIT_DIGEST_BYTES];
    PoisonAuditEvent events[POISON_AUDIT_RING_SIZE];
    size_t event_count;
    size_t write_index;
} PoisonAuditChain;

void poison_audit_init(PoisonAuditChain* chain);

void poison_audit_on_system_start(void);

PoisonAuditChain* poison_audit_get(void);

bool poison_audit_append(
    PoisonAuditChain* chain,
    const uint8_t actor_digest[POISON_AUDIT_DIGEST_BYTES],
    const char* action,
    const char* resource,
    PoisonAuditDecision decision,
    uint64_t timestamp_ms,
    const uint8_t correlation_id[POISON_AUDIT_DIGEST_BYTES],
    const char* safe_metadata,
    PoisonAuditEvent* event);

#ifdef __cplusplus
}
#endif
