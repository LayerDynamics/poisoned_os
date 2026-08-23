#include "poison_case.h"
#include "poison_evidence.h"
#include "poison_evidence_i.h"

#include <furi.h>
#include <storage/storage.h>
#include <stdio.h>
#include <string.h>

#define POISON_CASE_ROOT "/ext/evidence/cases"
#define POISON_CASE_TEMP "/ext/evidence/.tmp"
#define POISON_CASE_ENCODED_SIZE                                                          \
    (8u + (POISON_EVIDENCE_ID_MAX + 1u) + (POISON_EVIDENCE_CASE_NAME_MAX + 1u) +          \
     (POISON_EVIDENCE_CASE_PURPOSE_MAX + 1u) + (POISON_EVIDENCE_OWNER_ID_MAX + 1u) + 8u + \
     (POISON_EVIDENCE_RETENTION_MAX + 1u))

static bool poison_case_text_validate(const char* text, size_t maximum) {
    return text && text[0] != '\0' && strnlen(text, maximum + 1u) <= maximum;
}

static bool poison_case_mkdir(Storage* storage, const char* path) {
    const FS_Error result = storage_common_mkdir(storage, path);
    return result == FSE_OK || result == FSE_EXIST;
}

static bool poison_case_record_validate(const PoisonCaseRecord* record) {
    return record && poison_evidence_id_validate(record->case_id) &&
           poison_case_text_validate(record->name, POISON_EVIDENCE_CASE_NAME_MAX) &&
           poison_case_text_validate(record->purpose, POISON_EVIDENCE_CASE_PURPOSE_MAX) &&
           poison_evidence_id_validate(record->owner_id) && record->created_at_ms != 0u &&
           poison_case_text_validate(record->retention_policy, POISON_EVIDENCE_RETENTION_MAX);
}

static void
    poison_case_encode(const PoisonCaseRecord* record, uint8_t encoded[POISON_CASE_ENCODED_SIZE]) {
    memset(encoded, 0, POISON_CASE_ENCODED_SIZE);
    memcpy(encoded, "POISCASE", 8u);
    size_t offset = 8u;
#define POISON_CASE_ENCODE_STRING(value)                  \
    do {                                                  \
        memcpy(encoded + offset, (value), sizeof(value)); \
        offset += sizeof(value);                          \
    } while(false)
    POISON_CASE_ENCODE_STRING(record->case_id);
    POISON_CASE_ENCODE_STRING(record->name);
    POISON_CASE_ENCODE_STRING(record->purpose);
    POISON_CASE_ENCODE_STRING(record->owner_id);
    for(size_t index = 0u; index < 8u; ++index)
        encoded[offset++] = (uint8_t)(record->created_at_ms >> (index * 8u));
    POISON_CASE_ENCODE_STRING(record->retention_policy);
#undef POISON_CASE_ENCODE_STRING
    furi_check(offset == POISON_CASE_ENCODED_SIZE);
}

static bool
    poison_case_decode(const uint8_t encoded[POISON_CASE_ENCODED_SIZE], PoisonCaseRecord* record) {
    if(memcmp(encoded, "POISCASE", 8u) != 0) return false;
    memset(record, 0, sizeof(*record));
    size_t offset = 8u;
#define POISON_CASE_DECODE_STRING(value)                  \
    do {                                                  \
        memcpy((value), encoded + offset, sizeof(value)); \
        offset += sizeof(value);                          \
    } while(false)
    POISON_CASE_DECODE_STRING(record->case_id);
    POISON_CASE_DECODE_STRING(record->name);
    POISON_CASE_DECODE_STRING(record->purpose);
    POISON_CASE_DECODE_STRING(record->owner_id);
    for(size_t index = 0u; index < 8u; ++index)
        record->created_at_ms |= (uint64_t)encoded[offset++] << (index * 8u);
    POISON_CASE_DECODE_STRING(record->retention_policy);
#undef POISON_CASE_DECODE_STRING
    return offset == POISON_CASE_ENCODED_SIZE && poison_case_record_validate(record);
}

bool poison_case_create(PoisonEvidenceCase* evidence_case, const char* case_id, const char* name) {
    if(!evidence_case || evidence_case->active || !poison_evidence_id_validate(case_id) ||
       !poison_case_text_validate(name, POISON_EVIDENCE_CASE_NAME_MAX))
        return false;
    memset(evidence_case, 0, sizeof(*evidence_case));
    strcpy(evidence_case->case_id, case_id);
    strcpy(evidence_case->name, name);
    evidence_case->state = PoisonCaseOpen;
    evidence_case->active = true;
    return true;
}

bool poison_case_close(PoisonEvidenceCase* evidence_case) {
    if(!evidence_case || !evidence_case->active || evidence_case->state != PoisonCaseOpen)
        return false;
    evidence_case->state = PoisonCaseClosed;
    return true;
}

bool poison_case_exists_persistent(const char* case_id) {
    if(!poison_evidence_id_validate(case_id)) return false;
    char path[160u];
    snprintf(path, sizeof(path), POISON_CASE_ROOT "/%s.pcase", case_id);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    const bool exists = storage_file_exists(storage, path);
    furi_record_close(RECORD_STORAGE);
    return exists;
}

bool poison_case_load_persistent(const char* case_id, PoisonCaseRecord* record) {
    if(!poison_evidence_id_validate(case_id) || !record) return false;
    char path[160u];
    snprintf(path, sizeof(path), POISON_CASE_ROOT "/%s.pcase", case_id);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    uint8_t encoded[POISON_CASE_ENCODED_SIZE];
    bool loaded = file && storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
                  storage_file_size(file) == sizeof(encoded) &&
                  storage_file_read(file, encoded, sizeof(encoded)) == sizeof(encoded);
    if(file && storage_file_is_open(file)) loaded = storage_file_close(file) && loaded;
    if(file) storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    if(loaded)
        loaded = poison_case_decode(encoded, record) && strcmp(record->case_id, case_id) == 0;
    if(!loaded) memset(record, 0, sizeof(*record));
    memset(encoded, 0, sizeof(encoded));
    return loaded;
}

bool poison_case_create_persistent(const PoisonCaseRecord* record) {
    if(!poison_case_record_validate(record)) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool written = poison_case_mkdir(storage, "/ext/evidence") &&
                   poison_case_mkdir(storage, POISON_CASE_ROOT) &&
                   poison_case_mkdir(storage, POISON_CASE_TEMP);
    char temporary_path[192u];
    char final_path[160u];
    snprintf(temporary_path, sizeof(temporary_path), POISON_CASE_TEMP "/%s.case", record->case_id);
    snprintf(final_path, sizeof(final_path), POISON_CASE_ROOT "/%s.pcase", record->case_id);
    written = written && !storage_file_exists(storage, final_path);

    uint8_t encoded[POISON_CASE_ENCODED_SIZE];
    poison_case_encode(record, encoded);

    File* file = storage_file_alloc(storage);
    written = written && file &&
              storage_file_open(file, temporary_path, FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
              storage_file_write(file, encoded, sizeof(encoded)) == sizeof(encoded) &&
              storage_file_sync(file);
    if(file && storage_file_is_open(file)) written = storage_file_close(file) && written;
    if(file) storage_file_free(file);
    if(written) written = storage_common_rename(storage, temporary_path, final_path) == FSE_OK;
    if(!written) (void)storage_common_remove(storage, temporary_path);
    memset(encoded, 0, sizeof(encoded));
    furi_record_close(RECORD_STORAGE);
    return written;
}
