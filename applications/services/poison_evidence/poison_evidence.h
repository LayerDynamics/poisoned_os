#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <mbedtls/sha256.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_EVIDENCE_MAX_RECORDS (32u)

typedef struct {
    bool active;
    bool derived;
    char evidence_id[65];
    char case_id[65];
    uint64_t content_length;
    uint8_t content_sha256[32];
    uint8_t previous_audit_sha256[32];
    uint8_t audit_sha256[32];
} PoisonEvidenceRecord;

typedef struct {
    PoisonEvidenceRecord records[POISON_EVIDENCE_MAX_RECORDS];
    size_t count;
} PoisonEvidenceStore;

typedef struct {
    bool active;
    PoisonEvidenceRecord record;
    mbedtls_sha256_context hash;
    size_t expected_length;
    size_t received_length;
} PoisonEvidenceTransaction;

void poison_evidence_store_init(PoisonEvidenceStore* store);
bool poison_evidence_capture(
    PoisonEvidenceStore* store,
    const char* evidence_id,
    const char* case_id,
    const uint8_t* data,
    size_t length,
    bool derived,
    const uint8_t previous_audit_sha256[32]);
bool poison_evidence_begin(
    PoisonEvidenceStore* store,
    PoisonEvidenceTransaction* transaction,
    const char* evidence_id,
    const char* case_id,
    size_t expected_length,
    bool derived,
    const uint8_t previous_audit_sha256[32]);
bool poison_evidence_append(
    PoisonEvidenceTransaction* transaction,
    const uint8_t* data,
    size_t length);
bool poison_evidence_commit(PoisonEvidenceStore* store, PoisonEvidenceTransaction* transaction);
void poison_evidence_abort(PoisonEvidenceTransaction* transaction);
const PoisonEvidenceRecord*
    poison_evidence_find(const PoisonEvidenceStore* store, const char* evidence_id);
void poison_evidence_on_system_start(void);

#ifdef __cplusplus
}
#endif
