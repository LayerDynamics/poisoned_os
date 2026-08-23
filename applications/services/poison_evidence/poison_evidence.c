#include "poison_evidence.h"
#include "poison_evidence_i.h"

#include <furi.h>
#include <mbedtls/sha256.h>
#include <storage/storage.h>
#include <stdio.h>
#include <string.h>

#define POISON_EVIDENCE_ROOT        "/ext/evidence"
#define POISON_EVIDENCE_OBJECTS     POISON_EVIDENCE_ROOT "/objects"
#define POISON_EVIDENCE_RECORDS     POISON_EVIDENCE_ROOT "/records"
#define POISON_EVIDENCE_TEMP        POISON_EVIDENCE_ROOT "/.tmp"
#define POISON_EVIDENCE_RECORD_SIZE 243u

typedef struct {
    FuriMutex* mutex;
    PoisonEvidenceStore store;
} PoisonEvidenceService;

static PoisonEvidenceService* poison_evidence_service;

static void poison_evidence_digest_hex(const uint8_t digest[32], char output[65]) {
    for(size_t index = 0u; index < 32u; index++)
        snprintf(output + index * 2u, 3u, "%02x", digest[index]);
    output[64] = '\0';
}

static bool poison_evidence_mkdir(Storage* storage, const char* path) {
    const FS_Error status = storage_common_mkdir(storage, path);
    return status == FSE_OK || status == FSE_EXIST;
}

static bool poison_evidence_write_atomic(
    Storage* storage,
    const char* temporary_path,
    const char* final_path,
    const uint8_t* data,
    size_t length) {
    if(storage_file_exists(storage, final_path)) return false;
    File* file = storage_file_alloc(storage);
    if(!file) return false;
    const bool opened = storage_file_open(file, temporary_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    bool written = opened && storage_file_write(file, data, length) == length &&
                   storage_file_sync(file);
    if(opened) written = storage_file_close(file) && written;
    storage_file_free(file);
    if(written) written = storage_common_rename(storage, temporary_path, final_path) == FSE_OK;
    if(!written) (void)storage_common_remove(storage, temporary_path);
    return written;
}

static bool poison_evidence_object_matches(
    Storage* storage,
    const char* path,
    const uint8_t expected_digest[32],
    size_t expected_length) {
    File* file = storage_file_alloc(storage);
    if(!file || !storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) ||
       storage_file_size(file) != expected_length) {
        if(file) {
            if(storage_file_is_open(file)) storage_file_close(file);
            storage_file_free(file);
        }
        return false;
    }
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool valid = mbedtls_sha256_starts(&hash, 0) == 0;
    uint8_t buffer[256];
    size_t remaining = expected_length;
    while(valid && remaining) {
        const size_t requested = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const size_t received = storage_file_read(file, buffer, requested);
        valid = received == requested && mbedtls_sha256_update(&hash, buffer, received) == 0;
        remaining -= received;
    }
    uint8_t digest[32];
    if(valid) valid = mbedtls_sha256_finish(&hash, digest) == 0;
    mbedtls_sha256_free(&hash);
    storage_file_close(file);
    storage_file_free(file);
    return valid && memcmp(digest, expected_digest, sizeof(digest)) == 0;
}

static void poison_evidence_record_encode(
    const PoisonEvidenceRecord* record,
    uint8_t output[POISON_EVIDENCE_RECORD_SIZE]) {
    memset(output, 0, POISON_EVIDENCE_RECORD_SIZE);
    memcpy(output, "POISEV1", 7u);
    output[7] = record->derived ? 1u : 0u;
    memcpy(output + 8u, record->evidence_id, sizeof(record->evidence_id));
    memcpy(output + 73u, record->case_id, sizeof(record->case_id));
    for(size_t index = 0u; index < 8u; index++)
        output[138u + index] = (uint8_t)(record->content_length >> (index * 8u));
    memcpy(output + 146u, record->content_sha256, 32u);
    memcpy(output + 178u, record->previous_audit_sha256, 32u);
    memcpy(output + 210u, record->audit_sha256, 32u);
    output[242] = '\n';
}

static bool poison_evidence_persist(
    const PoisonEvidenceRecord* record,
    const uint8_t* data,
    size_t length) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool persisted = poison_evidence_mkdir(storage, POISON_EVIDENCE_ROOT) &&
                     poison_evidence_mkdir(storage, POISON_EVIDENCE_OBJECTS) &&
                     poison_evidence_mkdir(storage, POISON_EVIDENCE_RECORDS) &&
                     poison_evidence_mkdir(storage, POISON_EVIDENCE_TEMP);
    char digest[65];
    poison_evidence_digest_hex(record->content_sha256, digest);
    char object_path[160];
    char object_temporary[192];
    char record_path[192];
    char record_temporary[192];
    snprintf(object_path, sizeof(object_path), POISON_EVIDENCE_OBJECTS "/%s.bin", digest);
    snprintf(
        object_temporary,
        sizeof(object_temporary),
        POISON_EVIDENCE_TEMP "/%s.object",
        record->evidence_id);
    snprintf(
        record_path, sizeof(record_path), POISON_EVIDENCE_RECORDS "/%s.pev", record->evidence_id);
    snprintf(
        record_temporary,
        sizeof(record_temporary),
        POISON_EVIDENCE_TEMP "/%s.record",
        record->evidence_id);
    if(persisted && storage_file_exists(storage, object_path)) {
        persisted =
            poison_evidence_object_matches(storage, object_path, record->content_sha256, length);
    } else if(persisted) {
        persisted =
            poison_evidence_write_atomic(storage, object_temporary, object_path, data, length);
    }
    uint8_t encoded[POISON_EVIDENCE_RECORD_SIZE];
    poison_evidence_record_encode(record, encoded);
    if(persisted)
        persisted = poison_evidence_write_atomic(
            storage, record_temporary, record_path, encoded, sizeof(encoded));
    furi_record_close(RECORD_STORAGE);
    return persisted;
}

static bool poison_evidence_copy_object(
    Storage* storage,
    const char* source_path,
    const char* temporary_path,
    const char* object_path,
    const PoisonEvidenceRecord* record) {
    if(storage_file_exists(storage, object_path))
        return poison_evidence_object_matches(
            storage, object_path, record->content_sha256, record->content_length);
    File* source = storage_file_alloc(storage);
    File* output = storage_file_alloc(storage);
    bool copied = source && output &&
                  storage_file_open(source, source_path, FSAM_READ, FSOM_OPEN_EXISTING) &&
                  storage_file_size(source) == record->content_length &&
                  storage_file_open(output, temporary_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    uint8_t buffer[256u];
    size_t remaining = record->content_length;
    while(copied && remaining > 0u) {
        const size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        copied = storage_file_read(source, buffer, chunk) == chunk &&
                 storage_file_write(output, buffer, chunk) == chunk;
        remaining -= copied ? chunk : 0u;
    }
    if(copied) copied = storage_file_sync(output);
    if(source && storage_file_is_open(source)) copied = storage_file_close(source) && copied;
    if(output && storage_file_is_open(output)) copied = storage_file_close(output) && copied;
    if(source) storage_file_free(source);
    if(output) storage_file_free(output);
    memset(buffer, 0, sizeof(buffer));
    if(copied)
        copied = poison_evidence_object_matches(
            storage, temporary_path, record->content_sha256, record->content_length);
    if(copied && storage_file_exists(storage, object_path)) {
        copied = poison_evidence_object_matches(
            storage, object_path, record->content_sha256, record->content_length);
        (void)storage_common_remove(storage, temporary_path);
    } else if(copied) {
        copied = storage_common_rename(storage, temporary_path, object_path) == FSE_OK;
    }
    if(!copied) (void)storage_common_remove(storage, temporary_path);
    return copied;
}

static bool
    poison_evidence_persist_file(const PoisonEvidenceRecord* record, const char* source_path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool persisted = poison_evidence_mkdir(storage, POISON_EVIDENCE_ROOT) &&
                     poison_evidence_mkdir(storage, POISON_EVIDENCE_OBJECTS) &&
                     poison_evidence_mkdir(storage, POISON_EVIDENCE_RECORDS) &&
                     poison_evidence_mkdir(storage, POISON_EVIDENCE_TEMP);
    char digest[65u];
    poison_evidence_digest_hex(record->content_sha256, digest);
    char object_path[160u];
    char object_temporary[192u];
    char record_path[192u];
    char record_temporary[192u];
    snprintf(object_path, sizeof(object_path), POISON_EVIDENCE_OBJECTS "/%s.bin", digest);
    snprintf(
        object_temporary,
        sizeof(object_temporary),
        POISON_EVIDENCE_TEMP "/%s.object",
        record->evidence_id);
    snprintf(
        record_path, sizeof(record_path), POISON_EVIDENCE_RECORDS "/%s.pev", record->evidence_id);
    snprintf(
        record_temporary,
        sizeof(record_temporary),
        POISON_EVIDENCE_TEMP "/%s.record",
        record->evidence_id);
    if(persisted)
        persisted = poison_evidence_copy_object(
            storage, source_path, object_temporary, object_path, record);
    uint8_t encoded[POISON_EVIDENCE_RECORD_SIZE];
    poison_evidence_record_encode(record, encoded);
    if(persisted)
        persisted = poison_evidence_write_atomic(
            storage, record_temporary, record_path, encoded, sizeof(encoded));
    memset(encoded, 0, sizeof(encoded));
    furi_record_close(RECORD_STORAGE);
    return persisted;
}

static bool copy_id(char destination[65], const char* source) {
    if(!poison_evidence_id_validate(source)) return false;
    strcpy(destination, source);
    return true;
}

bool poison_evidence_id_validate(const char* identifier) {
    if(!identifier || identifier[0] == '\0' || identifier[0] == '.' ||
       strnlen(identifier, 65u) >= 65u) {
        return false;
    }
    for(const char* cursor = identifier; *cursor; ++cursor) {
        const bool allowed = (*cursor >= 'a' && *cursor <= 'z') ||
                             (*cursor >= 'A' && *cursor <= 'Z') ||
                             (*cursor >= '0' && *cursor <= '9') || *cursor == '.' ||
                             *cursor == '_' || *cursor == '-';
        if(!allowed) return false;
    }
    return true;
}

bool poison_evidence_record_exists_global(const char* evidence_id) {
    if(!poison_evidence_id_validate(evidence_id)) return false;
    char record_path[192u];
    snprintf(record_path, sizeof(record_path), POISON_EVIDENCE_RECORDS "/%s.pev", evidence_id);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    const bool exists = storage_file_exists(storage, record_path);
    furi_record_close(RECORD_STORAGE);
    return exists;
}

void poison_evidence_store_init(PoisonEvidenceStore* store) {
    if(store) memset(store, 0, sizeof(*store));
}

const PoisonEvidenceRecord*
    poison_evidence_find(const PoisonEvidenceStore* store, const char* evidence_id) {
    if(!store || !evidence_id) return NULL;
    for(size_t index = 0; index < store->count; ++index)
        if(store->records[index].active &&
           strcmp(store->records[index].evidence_id, evidence_id) == 0)
            return &store->records[index];
    return NULL;
}

void poison_evidence_on_system_start(void) {
    furi_check(!poison_evidence_service);
    poison_evidence_service = malloc(sizeof(*poison_evidence_service));
    furi_check(poison_evidence_service);
    memset(poison_evidence_service, 0, sizeof(*poison_evidence_service));
    poison_evidence_service->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    poison_evidence_store_init(&poison_evidence_service->store);
}

bool poison_evidence_capture_global(
    const char* evidence_id,
    const char* case_id,
    const uint8_t* data,
    size_t length,
    bool derived,
    const uint8_t previous_audit_sha256[32]) {
    if(!poison_evidence_service) return false;
    PoisonEvidenceStore* staging = malloc(sizeof(*staging));
    if(!staging) return false;
    poison_evidence_store_init(staging);
    bool captured = poison_evidence_capture(
        staging, evidence_id, case_id, data, length, derived, previous_audit_sha256);
    furi_check(
        furi_mutex_acquire(poison_evidence_service->mutex, FuriWaitForever) == FuriStatusOk);
    if(captured)
        captured = poison_evidence_service->store.count < POISON_EVIDENCE_MAX_RECORDS &&
                   !poison_evidence_find(&poison_evidence_service->store, evidence_id) &&
                   poison_evidence_persist(&staging->records[0], data, length);
    if(captured)
        poison_evidence_service->store.records[poison_evidence_service->store.count++] =
            staging->records[0];
    furi_check(furi_mutex_release(poison_evidence_service->mutex) == FuriStatusOk);
    free(staging);
    return captured;
}

bool poison_evidence_capture_file_global(
    const char* evidence_id,
    const char* case_id,
    const char* source_path,
    size_t expected_length,
    const uint8_t expected_sha256[32],
    bool derived,
    const uint8_t previous_audit_sha256[32],
    PoisonEvidenceRecord* captured_record) {
    if(!poison_evidence_service || !source_path || expected_length == 0u || !expected_sha256 ||
       !previous_audit_sha256 || !captured_record) {
        return false;
    }
    PoisonEvidenceStore* staging = malloc(sizeof(*staging));
    if(!staging) return false;
    poison_evidence_store_init(staging);
    PoisonEvidenceTransaction transaction = {0};
    bool captured = poison_evidence_begin(
        staging,
        &transaction,
        evidence_id,
        case_id,
        expected_length,
        derived,
        previous_audit_sha256);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* source = storage_file_alloc(storage);
    captured = captured && source &&
               storage_file_open(source, source_path, FSAM_READ, FSOM_OPEN_EXISTING) &&
               storage_file_size(source) == expected_length;
    uint8_t buffer[256u];
    size_t remaining = expected_length;
    while(captured && remaining > 0u) {
        const size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        captured = storage_file_read(source, buffer, chunk) == chunk &&
                   poison_evidence_append(&transaction, buffer, chunk);
        remaining -= captured ? chunk : 0u;
    }
    if(source && storage_file_is_open(source)) captured = storage_file_close(source) && captured;
    if(source) storage_file_free(source);
    furi_record_close(RECORD_STORAGE);
    memset(buffer, 0, sizeof(buffer));
    captured = captured && poison_evidence_commit(staging, &transaction) &&
               memcmp(staging->records[0].content_sha256, expected_sha256, 32u) == 0;
    if(!captured) poison_evidence_abort(&transaction);
    furi_check(
        furi_mutex_acquire(poison_evidence_service->mutex, FuriWaitForever) == FuriStatusOk);
    if(captured)
        captured = poison_evidence_service->store.count < POISON_EVIDENCE_MAX_RECORDS &&
                   !poison_evidence_find(&poison_evidence_service->store, evidence_id) &&
                   poison_evidence_persist_file(&staging->records[0], source_path);
    if(captured) {
        poison_evidence_service->store.records[poison_evidence_service->store.count++] =
            staging->records[0];
        *captured_record = staging->records[0];
    }
    furi_check(furi_mutex_release(poison_evidence_service->mutex) == FuriStatusOk);
    memset(staging, 0, sizeof(*staging));
    free(staging);
    return captured;
}

bool poison_evidence_capture(
    PoisonEvidenceStore* store,
    const char* evidence_id,
    const char* case_id,
    const uint8_t* data,
    size_t length,
    bool derived,
    const uint8_t previous_audit_sha256[32]) {
    if(!store || !data || length == 0) return false;
    PoisonEvidenceTransaction transaction = {0};
    if(!poison_evidence_begin(
           store, &transaction, evidence_id, case_id, length, derived, previous_audit_sha256) ||
       !poison_evidence_append(&transaction, data, length) ||
       !poison_evidence_commit(store, &transaction)) {
        poison_evidence_abort(&transaction);
        return false;
    }
    return true;
}

bool poison_evidence_begin(
    PoisonEvidenceStore* store,
    PoisonEvidenceTransaction* transaction,
    const char* evidence_id,
    const char* case_id,
    size_t expected_length,
    bool derived,
    const uint8_t previous_audit_sha256[32]) {
    if(!store || !transaction || expected_length == 0 || !previous_audit_sha256 ||
       store->count >= POISON_EVIDENCE_MAX_RECORDS || poison_evidence_find(store, evidence_id)) {
        return false;
    }
    memset(transaction, 0, sizeof(*transaction));
    if(!copy_id(transaction->record.evidence_id, evidence_id) ||
       !copy_id(transaction->record.case_id, case_id)) {
        return false;
    }
    transaction->record.content_length = expected_length;
    transaction->record.derived = derived;
    memcpy(transaction->record.previous_audit_sha256, previous_audit_sha256, 32);
    mbedtls_sha256_init(&transaction->hash);
    if(mbedtls_sha256_starts(&transaction->hash, 0) != 0) {
        mbedtls_sha256_free(&transaction->hash);
        return false;
    }
    transaction->expected_length = expected_length;
    transaction->active = true;
    return true;
}

bool poison_evidence_append(
    PoisonEvidenceTransaction* transaction,
    const uint8_t* data,
    size_t length) {
    if(!transaction || !transaction->active || (!data && length != 0) ||
       length > transaction->expected_length - transaction->received_length)
        return false;
    if(mbedtls_sha256_update(&transaction->hash, data, length) != 0) return false;
    transaction->received_length += length;
    return true;
}

bool poison_evidence_commit(PoisonEvidenceStore* store, PoisonEvidenceTransaction* transaction) {
    if(!store || !transaction || !transaction->active ||
       transaction->received_length != transaction->expected_length ||
       store->count >= POISON_EVIDENCE_MAX_RECORDS ||
       poison_evidence_find(store, transaction->record.evidence_id))
        return false;
    if(mbedtls_sha256_finish(&transaction->hash, transaction->record.content_sha256) != 0) {
        poison_evidence_abort(transaction);
        return false;
    }
    static const uint8_t audit_domain[] = "POISON-EVIDENCE-AUDIT-v1";
    uint8_t derived = transaction->record.derived ? 1u : 0u;
    uint8_t content_length[8];
    for(size_t index = 0u; index < sizeof(content_length); index++)
        content_length[index] = (uint8_t)(transaction->record.content_length >> (index * 8u));
    mbedtls_sha256_context audit;
    mbedtls_sha256_init(&audit);
    bool audit_ok =
        mbedtls_sha256_starts(&audit, 0) == 0 &&
        mbedtls_sha256_update(&audit, audit_domain, sizeof(audit_domain) - 1u) == 0 &&
        mbedtls_sha256_update(&audit, transaction->record.previous_audit_sha256, 32u) == 0 &&
        mbedtls_sha256_update(
            &audit,
            (const uint8_t*)transaction->record.evidence_id,
            strlen(transaction->record.evidence_id) + 1u) == 0 &&
        mbedtls_sha256_update(
            &audit,
            (const uint8_t*)transaction->record.case_id,
            strlen(transaction->record.case_id) + 1u) == 0 &&
        mbedtls_sha256_update(&audit, &derived, sizeof(derived)) == 0 &&
        mbedtls_sha256_update(&audit, content_length, sizeof(content_length)) == 0 &&
        mbedtls_sha256_update(&audit, transaction->record.content_sha256, 32u) == 0 &&
        mbedtls_sha256_finish(&audit, transaction->record.audit_sha256) == 0;
    mbedtls_sha256_free(&audit);
    if(!audit_ok) {
        poison_evidence_abort(transaction);
        return false;
    }
    transaction->record.active = true;
    store->records[store->count++] = transaction->record;
    transaction->active = false;
    mbedtls_sha256_free(&transaction->hash);
    return true;
}

void poison_evidence_abort(PoisonEvidenceTransaction* transaction) {
    if(!transaction) return;
    if(transaction->active) mbedtls_sha256_free(&transaction->hash);
    memset(transaction, 0, sizeof(*transaction));
}
