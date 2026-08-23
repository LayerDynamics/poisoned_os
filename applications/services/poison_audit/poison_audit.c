#include "poison_audit_i.h"

#include <mbedtls/sha256.h>

#include <furi.h>
#include <applications/services/poison_startup.h>

#include <string.h>

static PoisonAuditChain poison_audit_chain;
static FuriMutex* poison_audit_mutex;

static void poison_audit_lock(const PoisonAuditChain* chain) {
    if(chain == &poison_audit_chain && poison_audit_mutex) {
        furi_check(furi_mutex_acquire(poison_audit_mutex, FuriWaitForever) == FuriStatusOk);
    }
}

static void poison_audit_unlock(const PoisonAuditChain* chain) {
    if(chain == &poison_audit_chain && poison_audit_mutex) {
        furi_check(furi_mutex_release(poison_audit_mutex) == FuriStatusOk);
    }
}

static bool audit_digest_valid(const uint8_t* digest) {
    if(!digest) return false;
    uint8_t nonzero = 0;
    for(size_t index = 0; index < POISON_AUDIT_DIGEST_BYTES; ++index)
        nonzero |= digest[index];
    return nonzero != 0;
}

static bool audit_text_valid(const char* value, size_t max_length) {
    if(!value || value[0] == '\0') return false;
    size_t length = strnlen(value, max_length + 1u);
    if(length == 0 || length > max_length) return false;
    for(size_t index = 0; index < length; ++index) {
        if((unsigned char)value[index] < 0x20 || (unsigned char)value[index] > 0x7e) return false;
    }
    return true;
}

static bool audit_metadata_safe(const char* metadata) {
    if(!metadata) return false;
    const char* forbidden[] = {"key", "secret", "token", "payload", "private"};
    for(size_t index = 0; index < sizeof(forbidden) / sizeof(forbidden[0]); ++index) {
        if(strstr(metadata, forbidden[index])) return false;
    }
    return audit_text_valid(metadata, POISON_AUDIT_METADATA_MAX);
}

void poison_audit_init(PoisonAuditChain* chain) {
    if(!chain) return;
    memset(chain, 0, sizeof(*chain));
    chain->next_event_id = 1;
}

void poison_audit_on_system_start(void) {
    if(!poison_startup_is_runtime_boot()) return;
    poison_audit_init(&poison_audit_chain);
    furi_check(!poison_audit_mutex);
    poison_audit_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
}

PoisonAuditChain* poison_audit_get(void) {
    return &poison_audit_chain;
}

bool poison_audit_append(
    PoisonAuditChain* chain,
    const uint8_t actor_digest[POISON_AUDIT_DIGEST_BYTES],
    const char* action,
    const char* resource,
    PoisonAuditDecision decision,
    uint64_t timestamp_ms,
    const uint8_t correlation_id[POISON_AUDIT_DIGEST_BYTES],
    const char* safe_metadata,
    PoisonAuditEvent* event) {
    if(!chain || !event || chain->next_event_id == 0 || !audit_digest_valid(actor_digest) ||
       !audit_text_valid(action, POISON_AUDIT_TEXT_MAX) ||
       !audit_text_valid(resource, POISON_AUDIT_TEXT_MAX) ||
       decision > PoisonAuditDecisionRevoked || !audit_digest_valid(correlation_id) ||
       !audit_metadata_safe(safe_metadata))
        return false;
    poison_audit_lock(chain);
    memset(event, 0, sizeof(*event));
    event->event_id = chain->next_event_id;
    memcpy(event->prior_digest, chain->last_digest, sizeof(event->prior_digest));
    memcpy(event->actor_digest, actor_digest, sizeof(event->actor_digest));
    memcpy(event->correlation_id, correlation_id, sizeof(event->correlation_id));
    memcpy(event->action, action, strlen(action) + 1u);
    memcpy(event->resource, resource, strlen(resource) + 1u);
    memcpy(event->safe_metadata, safe_metadata, strlen(safe_metadata) + 1u);
    event->decision = decision;
    event->timestamp_ms = timestamp_ms;

    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool ok = mbedtls_sha256_starts(&hash, 0) == 0;
    ok = ok &&
         mbedtls_sha256_update(&hash, (const uint8_t*)event, offsetof(PoisonAuditEvent, digest)) ==
             0;
    ok = ok && mbedtls_sha256_finish(&hash, event->digest) == 0;
    mbedtls_sha256_free(&hash);
    if(!ok) {
        memset(event, 0, sizeof(*event));
        poison_audit_unlock(chain);
        return false;
    }
    memcpy(chain->last_digest, event->digest, sizeof(chain->last_digest));
    chain->events[chain->write_index] = *event;
    chain->write_index = (chain->write_index + 1u) % POISON_AUDIT_RING_SIZE;
    if(chain->event_count < POISON_AUDIT_RING_SIZE) chain->event_count++;
    ++chain->next_event_id;
    poison_audit_unlock(chain);
    return true;
}

size_t poison_audit_snapshot(
    const PoisonAuditChain* chain,
    uint64_t after_event_id,
    PoisonAuditEvent* events,
    size_t event_capacity,
    bool* truncated,
    uint64_t* next_event_id,
    uint8_t last_digest[POISON_AUDIT_DIGEST_BYTES]) {
    if(!chain || !events || event_capacity == 0u || event_capacity > POISON_AUDIT_RING_SIZE ||
       !truncated || !next_event_id || !last_digest) {
        return 0u;
    }
    poison_audit_lock(chain);
    const uint64_t oldest_event_id = chain->next_event_id - chain->event_count;
    size_t available = 0u;
    for(uint64_t event_id = oldest_event_id; event_id < chain->next_event_id; ++event_id) {
        if(event_id > after_event_id) available++;
    }
    *truncated = (chain->event_count != 0u && after_event_id < oldest_event_id - 1u) ||
                 available > event_capacity;
    *next_event_id = chain->next_event_id;
    memcpy(last_digest, chain->last_digest, POISON_AUDIT_DIGEST_BYTES);
    size_t copied = 0u;
    for(uint64_t event_id = oldest_event_id;
        event_id < chain->next_event_id && copied < event_capacity;
        ++event_id) {
        if(event_id <= after_event_id) continue;
        for(size_t index = 0u; index < POISON_AUDIT_RING_SIZE; ++index) {
            if(chain->events[index].event_id == event_id) {
                events[copied++] = chain->events[index];
                break;
            }
        }
    }
    poison_audit_unlock(chain);
    return copied;
}
