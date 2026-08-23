#pragma once

#include "poison_evidence.h"

#define POISON_EVIDENCE_CASE_PURPOSE_MAX (256u)
#define POISON_EVIDENCE_OWNER_ID_MAX     (64u)
#define POISON_EVIDENCE_RETENTION_MAX    (64u)

typedef struct {
    char case_id[65u];
    char name[129u];
    char purpose[POISON_EVIDENCE_CASE_PURPOSE_MAX + 1u];
    char owner_id[POISON_EVIDENCE_OWNER_ID_MAX + 1u];
    uint64_t created_at_ms;
    char retention_policy[POISON_EVIDENCE_RETENTION_MAX + 1u];
} PoisonCaseRecord;

bool poison_evidence_id_validate(const char* identifier);
bool poison_case_create_persistent(const PoisonCaseRecord* record);
bool poison_case_exists_persistent(const char* case_id);
bool poison_case_load_persistent(const char* case_id, PoisonCaseRecord* record);

bool poison_evidence_capture_global(
    const char* evidence_id,
    const char* case_id,
    const uint8_t* data,
    size_t length,
    bool derived,
    const uint8_t previous_audit_sha256[32]);

bool poison_evidence_capture_file_global(
    const char* evidence_id,
    const char* case_id,
    const char* source_path,
    size_t expected_length,
    const uint8_t expected_sha256[32],
    bool derived,
    const uint8_t previous_audit_sha256[32],
    PoisonEvidenceRecord* captured_record);

bool poison_evidence_record_exists_global(const char* evidence_id);
