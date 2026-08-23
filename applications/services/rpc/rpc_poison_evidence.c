#include "rpc_poison_evidence.h"
#include "rpc_i.h"

#include <applications/services/poison_evidence/poison_annotation.h>
#include <applications/services/poison_evidence/poison_case.h>
#include <applications/services/poison_evidence/poison_evidence_i.h>
#include <applications/services/poison_vfs/poison_vfs_paths.h>
#include <datetime/datetime.h>
#include <furi.h>
#include <furi_hal.h>
#include <mbedtls/sha256.h>
#include <storage/storage.h>

#include <stdio.h>
#include <string.h>

#define POISON_EXPORT_ROOT         "/ext/evidence/exports"
#define POISON_EXPORT_TEMP         "/ext/evidence/.tmp"
#define POISON_EXPORT_SCHEMA       "poison.evidence-manifest/v1"
#define POISON_EXPORT_BATCH_MAX    (8u)
#define POISON_EXPORT_EVIDENCE_MAX (10000u)

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
    const uint8_t previous_audit_sha256[32]) {
    if(!session || !store || !evidence_id || !case_id || !previous_audit_sha256 || !channel ||
       strcmp(channel, "evidence") != 0 ||
       poison_session_authenticate_rx(
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
    return poison_evidence_capture(
        store, evidence_id, case_id, payload, payload_length, derived, previous_audit_sha256);
}

typedef struct {
    RpcSession* session;
    bool decoded_export_valid;
    size_t decoded_export_count;
    char decoded_export_ids[POISON_EXPORT_BATCH_MAX][65u];
    bool export_active;
    bool export_hash_initialized;
    bool export_completed;
    char export_id[65u];
    char export_schema[65u];
    char export_last_evidence_id[65u];
    uint32_t export_next_batch;
    uint32_t export_accepted_count;
    uint32_t export_last_batch;
    bool export_last_batch_finalize;
    uint8_t export_last_batch_sha256[32u];
    uint8_t export_final_sha256[32u];
    mbedtls_sha256_context export_hash;
} RpcPoisonEvidence;

static bool rpc_poison_evidence_mkdir(Storage* storage, const char* path) {
    const FS_Error result = storage_common_mkdir(storage, path);
    return result == FSE_OK || result == FSE_EXIST;
}

static uint64_t rpc_poison_evidence_timestamp_ms(void) {
    DateTime datetime;
    furi_hal_rtc_get_datetime(&datetime);
    return (uint64_t)datetime_datetime_to_timestamp(&datetime) * 1000u;
}

static void rpc_poison_evidence_actor_id(uint64_t session_id, char output[65u]) {
    snprintf(output, 65u, "session-%016llx", (unsigned long long)session_id);
}

static void rpc_poison_evidence_export_paths(
    const char* export_id,
    char temporary_path[192u],
    char final_path[192u]) {
    snprintf(temporary_path, 192u, POISON_EXPORT_TEMP "/%s.export", export_id);
    snprintf(final_path, 192u, POISON_EXPORT_ROOT "/%s.pmanifest", export_id);
}

static void rpc_poison_evidence_export_reset(RpcPoisonEvidence* evidence) {
    if(evidence->export_hash_initialized) {
        mbedtls_sha256_free(&evidence->export_hash);
        evidence->export_hash_initialized = false;
    }
    if(evidence->export_active && poison_evidence_id_validate(evidence->export_id)) {
        char temporary_path[192u];
        char final_path[192u];
        rpc_poison_evidence_export_paths(evidence->export_id, temporary_path, final_path);
        (void)final_path;
        Storage* storage = furi_record_open(RECORD_STORAGE);
        (void)storage_common_remove(storage, temporary_path);
        furi_record_close(RECORD_STORAGE);
    }
    evidence->export_active = false;
    evidence->export_completed = false;
    evidence->export_id[0] = '\0';
    evidence->export_schema[0] = '\0';
    evidence->export_last_evidence_id[0] = '\0';
    evidence->export_next_batch = 0u;
    evidence->export_accepted_count = 0u;
}

static bool rpc_poison_evidence_export_id_decode(
    pb_istream_t* stream,
    const pb_field_t* field,
    void** argument) {
    (void)field;
    RpcPoisonEvidence* evidence = *argument;
    if(!evidence || stream->bytes_left == 0u || stream->bytes_left > 64u ||
       evidence->decoded_export_count >= POISON_EXPORT_BATCH_MAX) {
        if(evidence) evidence->decoded_export_valid = false;
        return false;
    }
    char* destination = evidence->decoded_export_ids[evidence->decoded_export_count];
    const size_t length = stream->bytes_left;
    if(!pb_read(stream, (pb_byte_t*)destination, length)) {
        evidence->decoded_export_valid = false;
        return false;
    }
    destination[length] = '\0';
    if(!poison_evidence_id_validate(destination)) {
        evidence->decoded_export_valid = false;
        return false;
    }
    evidence->decoded_export_count++;
    return true;
}

static bool rpc_poison_evidence_decode_submessage(
    pb_istream_t* stream,
    const pb_field_t* field,
    void** argument) {
    (void)stream;
    RpcPoisonEvidence* evidence = *argument;
    if(!evidence) return false;
    if(field->tag == PB_Main_poison_export_manifest_tag) {
        evidence->decoded_export_valid = true;
        evidence->decoded_export_count = 0u;
        PB_Poison_ExportManifest* manifest = field->pData;
        manifest->evidence_ids.funcs.decode = rpc_poison_evidence_export_id_decode;
        manifest->evidence_ids.arg = evidence;
    }
    return true;
}

static bool rpc_poison_evidence_hex_decode(const char* input, uint8_t output[32u]) {
    if(!input || strlen(input) != 64u) return false;
    for(size_t index = 0u; index < 32u; ++index) {
        const char high = input[index * 2u];
        const char low = input[index * 2u + 1u];
        if(!((high >= '0' && high <= '9') || (high >= 'a' && high <= 'f')) ||
           !((low >= '0' && low <= '9') || (low >= 'a' && low <= 'f'))) {
            return false;
        }
        output[index] = (uint8_t)(((high <= '9' ? high - '0' : high - 'a' + 10) << 4u) |
                                  (low <= '9' ? low - '0' : low - 'a' + 10));
    }
    return true;
}

static void rpc_poison_evidence_hex_encode(const uint8_t input[32u], char output[65u]) {
    static const char digits[] = "0123456789abcdef";
    for(size_t index = 0u; index < 32u; ++index) {
        output[index * 2u] = digits[input[index] >> 4u];
        output[index * 2u + 1u] = digits[input[index] & 0x0Fu];
    }
    output[64u] = '\0';
}

static void rpc_poison_evidence_record_process(const PB_Main* request, void* context) {
    RpcPoisonEvidence* evidence = context;
    const PB_Poison_EvidenceRecord* input = &request->content.poison_evidence_record;
    uint64_t session_id = 0u;
    PoisonRole role = PoisonRoleObserver;
    PoisonVfsResolvedPath source;
    uint8_t content_sha256[32u];
    uint8_t previous_audit_sha256[32u] = {0};
    bool previous_valid =
        input->previous_audit_sha256[0] == '\0' ||
        rpc_poison_evidence_hex_decode(input->previous_audit_sha256, previous_audit_sha256);
    bool valid =
        request->which_content == PB_Main_poison_evidence_record_tag && !request->has_next &&
        rpc_session_is_secure_dispatch_active(evidence->session) &&
        rpc_session_get_secure_identity(evidence->session, &session_id, &role) &&
        input->content_length > 0u && input->content_length <= 16u * 1024u * 1024u &&
        rpc_poison_evidence_hex_decode(input->content_sha256, content_sha256) && previous_valid &&
        poison_case_exists_persistent(input->case_id) &&
        poison_vfs_resolve_path(input->source_path, role, PoisonVfsOperationRead, &source);
    (void)session_id;
    PoisonEvidenceRecord captured = {0};
    if(valid)
        valid = poison_evidence_capture_file_global(
            input->evidence_id,
            input->case_id,
            source.backing_path,
            input->content_length,
            content_sha256,
            false,
            previous_audit_sha256,
            &captured);
    memset(content_sha256, 0, sizeof(content_sha256));
    memset(previous_audit_sha256, 0, sizeof(previous_audit_sha256));
    if(!valid) {
        memset(&captured, 0, sizeof(captured));
        rpc_send_and_release_empty(
            evidence->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }

    PB_Main* response = rpc_message_alloc();
    *response = (PB_Main)PB_Main_init_zero;
    response->command_id = request->command_id;
    response->command_status = PB_CommandStatus_OK;
    response->which_content = PB_Main_poison_evidence_record_tag;
    PB_Poison_EvidenceRecord* output = &response->content.poison_evidence_record;
    strcpy(output->evidence_id, captured.evidence_id);
    strcpy(output->case_id, captured.case_id);
    output->content_length = captured.content_length;
    strcpy(output->source_path, input->source_path);
    rpc_poison_evidence_hex_encode(captured.content_sha256, output->content_sha256);
    rpc_poison_evidence_hex_encode(captured.previous_audit_sha256, output->previous_audit_sha256);
    rpc_poison_evidence_hex_encode(captured.audit_sha256, output->audit_sha256);
    memset(&captured, 0, sizeof(captured));
    rpc_send_and_release(evidence->session, response);
    free(response);
}

static bool rpc_poison_evidence_identity(
    RpcPoisonEvidence* evidence,
    uint64_t* session_id,
    PoisonRole* role,
    char actor_id[65u]) {
    const bool valid = evidence && rpc_session_is_secure_dispatch_active(evidence->session) &&
                       rpc_session_get_secure_identity(evidence->session, session_id, role);
    if(valid) rpc_poison_evidence_actor_id(*session_id, actor_id);
    return valid;
}

static void rpc_poison_case_process(const PB_Main* request, void* context) {
    RpcPoisonEvidence* evidence = context;
    const PB_Poison_Case* input = &request->content.poison_case;
    uint64_t session_id = 0u;
    PoisonRole role = PoisonRoleObserver;
    char actor_id[65u] = {0};
    PoisonCaseRecord record = {0};
    bool valid = request->which_content == PB_Main_poison_case_tag && !request->has_next &&
                 rpc_poison_evidence_identity(evidence, &session_id, &role, actor_id) &&
                 (input->owner_id[0] == '\0' || strcmp(input->owner_id, actor_id) == 0) &&
                 poison_evidence_id_validate(input->case_id) && input->name[0] != '\0' &&
                 input->purpose[0] != '\0' && input->retention_policy[0] != '\0';
    (void)session_id;
    (void)role;
    if(valid) {
        strcpy(record.case_id, input->case_id);
        strcpy(record.name, input->name);
        strcpy(record.purpose, input->purpose);
        strcpy(record.owner_id, actor_id);
        strcpy(record.retention_policy, input->retention_policy);
        if(poison_case_exists_persistent(input->case_id)) {
            PoisonCaseRecord existing = {0};
            valid = poison_case_load_persistent(input->case_id, &existing) &&
                    strcmp(existing.name, record.name) == 0 &&
                    strcmp(existing.purpose, record.purpose) == 0 &&
                    strcmp(existing.owner_id, record.owner_id) == 0 &&
                    strcmp(existing.retention_policy, record.retention_policy) == 0;
            if(valid) record = existing;
            memset(&existing, 0, sizeof(existing));
        } else {
            record.created_at_ms = rpc_poison_evidence_timestamp_ms();
            valid = poison_case_create_persistent(&record);
        }
    }
    memset(actor_id, 0, sizeof(actor_id));
    if(!valid) {
        memset(&record, 0, sizeof(record));
        rpc_send_and_release_empty(
            evidence->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }

    PB_Main* response = rpc_message_alloc();
    *response = (PB_Main)PB_Main_init_zero;
    response->command_id = request->command_id;
    response->command_status = PB_CommandStatus_OK;
    response->which_content = PB_Main_poison_case_tag;
    PB_Poison_Case* output = &response->content.poison_case;
    strcpy(output->case_id, record.case_id);
    strcpy(output->name, record.name);
    strcpy(output->purpose, record.purpose);
    strcpy(output->owner_id, record.owner_id);
    output->created_at_ms = record.created_at_ms;
    strcpy(output->retention_policy, record.retention_policy);
    memset(&record, 0, sizeof(record));
    rpc_send_and_release(evidence->session, response);
    free(response);
}

static void rpc_poison_annotation_process(const PB_Main* request, void* context) {
    RpcPoisonEvidence* evidence = context;
    const PB_Poison_Annotation* input = &request->content.poison_annotation;
    uint64_t session_id = 0u;
    PoisonRole role = PoisonRoleObserver;
    char actor_id[65u] = {0};
    PoisonAnnotationRecord* record = malloc(sizeof(*record));
    if(record) memset(record, 0, sizeof(*record));
    bool valid = request->which_content == PB_Main_poison_annotation_tag && !request->has_next &&
                 record && rpc_poison_evidence_identity(evidence, &session_id, &role, actor_id) &&
                 (input->author_id[0] == '\0' || strcmp(input->author_id, actor_id) == 0) &&
                 input->tags_count <= POISON_ANNOTATION_TAGS_MAX &&
                 poison_annotation_validate(input->annotation_id, input->evidence_id, input->text);
    (void)session_id;
    (void)role;
    if(valid) {
        strcpy(record->annotation_id, input->annotation_id);
        strcpy(record->evidence_id, input->evidence_id);
        strcpy(record->author_id, actor_id);
        record->created_at_ms = rpc_poison_evidence_timestamp_ms();
        strcpy(record->text, input->text);
        record->tags_count = input->tags_count;
        for(size_t index = 0u; index < record->tags_count; ++index)
            strcpy(record->tags[index], input->tags[index]);
        valid = poison_annotation_append_persistent(record);
    }
    memset(actor_id, 0, sizeof(actor_id));
    if(!valid) {
        if(record) {
            memset(record, 0, sizeof(*record));
            free(record);
        }
        rpc_send_and_release_empty(
            evidence->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }

    PB_Main* response = rpc_message_alloc();
    *response = (PB_Main)PB_Main_init_zero;
    response->command_id = request->command_id;
    response->command_status = PB_CommandStatus_OK;
    response->which_content = PB_Main_poison_annotation_tag;
    PB_Poison_Annotation* output = &response->content.poison_annotation;
    strcpy(output->annotation_id, record->annotation_id);
    strcpy(output->evidence_id, record->evidence_id);
    strcpy(output->author_id, record->author_id);
    output->created_at_ms = record->created_at_ms;
    strcpy(output->text, record->text);
    output->tags_count = record->tags_count;
    for(size_t index = 0u; index < record->tags_count; ++index)
        strcpy(output->tags[index], record->tags[index]);
    memset(record, 0, sizeof(*record));
    free(record);
    rpc_send_and_release(evidence->session, response);
    free(response);
}

static bool rpc_poison_export_batch_digest(RpcPoisonEvidence* evidence, uint8_t digest[32u]) {
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    static const uint8_t domain[] = "POISON-EXPORT-BATCH-v1";
    bool valid = mbedtls_sha256_starts(&hash, 0) == 0 &&
                 mbedtls_sha256_update(&hash, domain, sizeof(domain) - 1u) == 0;
    for(size_t index = 0u; valid && index < evidence->decoded_export_count; ++index) {
        valid = mbedtls_sha256_update(
                    &hash,
                    (const uint8_t*)evidence->decoded_export_ids[index],
                    strlen(evidence->decoded_export_ids[index]) + 1u) == 0;
    }
    if(valid) valid = mbedtls_sha256_finish(&hash, digest) == 0;
    mbedtls_sha256_free(&hash);
    return valid;
}

static bool
    rpc_poison_export_begin(RpcPoisonEvidence* evidence, const PB_Poison_ExportManifest* input) {
    char temporary_path[192u];
    char final_path[192u];
    rpc_poison_evidence_export_paths(input->export_id, temporary_path, final_path);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool valid = rpc_poison_evidence_mkdir(storage, "/ext/evidence") &&
                 rpc_poison_evidence_mkdir(storage, POISON_EXPORT_ROOT) &&
                 rpc_poison_evidence_mkdir(storage, POISON_EXPORT_TEMP) &&
                 !storage_file_exists(storage, final_path);
    File* file = storage_file_alloc(storage);
    valid = valid && file &&
            storage_file_open(file, temporary_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    static const uint8_t magic[8u] = {'P', 'O', 'I', 'S', 'X', 'M', '1', '\0'};
    if(valid) valid = storage_file_write(file, magic, sizeof(magic)) == sizeof(magic);
    if(valid)
        valid = storage_file_write(file, input->export_id, sizeof(input->export_id)) ==
                sizeof(input->export_id);
    if(valid)
        valid = storage_file_write(file, input->schema, sizeof(input->schema)) ==
                sizeof(input->schema);
    if(valid) valid = storage_file_sync(file);
    if(file && storage_file_is_open(file)) valid = storage_file_close(file) && valid;
    if(file) storage_file_free(file);
    if(!valid) (void)storage_common_remove(storage, temporary_path);
    furi_record_close(RECORD_STORAGE);
    if(!valid) return false;

    mbedtls_sha256_init(&evidence->export_hash);
    evidence->export_hash_initialized = true;
    static const uint8_t domain[] = "POISON-EVIDENCE-MANIFEST-v1";
    valid =
        mbedtls_sha256_starts(&evidence->export_hash, 0) == 0 &&
        mbedtls_sha256_update(&evidence->export_hash, domain, sizeof(domain) - 1u) == 0 &&
        mbedtls_sha256_update(
            &evidence->export_hash,
            (const uint8_t*)input->export_id,
            strlen(input->export_id) + 1u) == 0 &&
        mbedtls_sha256_update(
            &evidence->export_hash, (const uint8_t*)input->schema, strlen(input->schema) + 1u) ==
            0;
    if(!valid) {
        mbedtls_sha256_free(&evidence->export_hash);
        evidence->export_hash_initialized = false;
        Storage* cleanup_storage = furi_record_open(RECORD_STORAGE);
        (void)storage_common_remove(cleanup_storage, temporary_path);
        furi_record_close(RECORD_STORAGE);
        return false;
    }
    strcpy(evidence->export_id, input->export_id);
    strcpy(evidence->export_schema, input->schema);
    evidence->export_active = true;
    evidence->export_completed = false;
    evidence->export_next_batch = 0u;
    evidence->export_accepted_count = 0u;
    evidence->export_last_evidence_id[0] = '\0';
    return true;
}

static bool rpc_poison_export_append(RpcPoisonEvidence* evidence) {
    char temporary_path[192u];
    char final_path[192u];
    rpc_poison_evidence_export_paths(evidence->export_id, temporary_path, final_path);
    (void)final_path;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool written = file && storage_file_open(file, temporary_path, FSAM_WRITE, FSOM_OPEN_APPEND);
    for(size_t index = 0u; written && index < evidence->decoded_export_count; ++index) {
        written = storage_file_write(
                      file,
                      evidence->decoded_export_ids[index],
                      sizeof(evidence->decoded_export_ids[index])) ==
                  sizeof(evidence->decoded_export_ids[index]);
    }
    if(written) written = storage_file_sync(file);
    if(file && storage_file_is_open(file)) written = storage_file_close(file) && written;
    if(file) storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return written;
}

static bool rpc_poison_export_finalize(RpcPoisonEvidence* evidence) {
    uint8_t count[4u];
    for(size_t index = 0u; index < sizeof(count); ++index)
        count[index] = (uint8_t)(evidence->export_accepted_count >> (index * 8u));
    bool valid = mbedtls_sha256_update(&evidence->export_hash, count, sizeof(count)) == 0 &&
                 mbedtls_sha256_finish(&evidence->export_hash, evidence->export_final_sha256) == 0;
    mbedtls_sha256_free(&evidence->export_hash);
    evidence->export_hash_initialized = false;

    char temporary_path[192u];
    char final_path[192u];
    rpc_poison_evidence_export_paths(evidence->export_id, temporary_path, final_path);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    valid = valid && file && storage_file_open(file, temporary_path, FSAM_WRITE, FSOM_OPEN_APPEND);
    static const uint8_t footer[8u] = {'P', 'O', 'I', 'S', 'E', 'N', 'D', '1'};
    if(valid) valid = storage_file_write(file, footer, sizeof(footer)) == sizeof(footer);
    if(valid) valid = storage_file_write(file, count, sizeof(count)) == sizeof(count);
    if(valid)
        valid = storage_file_write(
                    file, evidence->export_final_sha256, sizeof(evidence->export_final_sha256)) ==
                sizeof(evidence->export_final_sha256);
    if(valid) valid = storage_file_sync(file);
    if(file && storage_file_is_open(file)) valid = storage_file_close(file) && valid;
    if(file) storage_file_free(file);
    if(valid) valid = storage_common_rename(storage, temporary_path, final_path) == FSE_OK;
    if(!valid) (void)storage_common_remove(storage, temporary_path);
    furi_record_close(RECORD_STORAGE);
    memset(count, 0, sizeof(count));
    if(valid) {
        evidence->export_active = false;
        evidence->export_completed = true;
    }
    return valid;
}

static void rpc_poison_export_send_response(
    RpcPoisonEvidence* evidence,
    const PB_Main* request,
    bool finalized) {
    PB_Main* response = rpc_message_alloc();
    *response = (PB_Main)PB_Main_init_zero;
    response->command_id = request->command_id;
    response->command_status = PB_CommandStatus_OK;
    response->which_content = PB_Main_poison_export_manifest_tag;
    PB_Poison_ExportManifest* output = &response->content.poison_export_manifest;
    strcpy(output->export_id, evidence->export_id);
    strcpy(output->schema, evidence->export_schema);
    output->batch_index = evidence->export_last_batch;
    output->finalize = finalized;
    output->accepted_evidence_ids = evidence->export_accepted_count;
    if(finalized)
        rpc_poison_evidence_hex_encode(evidence->export_final_sha256, output->manifest_sha256);
    rpc_send_and_release(evidence->session, response);
    free(response);
}

static void rpc_poison_export_manifest_process(const PB_Main* request, void* context) {
    RpcPoisonEvidence* evidence = context;
    const PB_Poison_ExportManifest* input = &request->content.poison_export_manifest;
    uint64_t session_id = 0u;
    PoisonRole role = PoisonRoleObserver;
    char actor_id[65u] = {0};
    uint8_t batch_sha256[32u];
    bool valid =
        request->which_content == PB_Main_poison_export_manifest_tag && !request->has_next &&
        evidence->decoded_export_valid && evidence->decoded_export_count > 0u &&
        rpc_poison_evidence_identity(evidence, &session_id, &role, actor_id) &&
        poison_evidence_id_validate(input->export_id) &&
        strcmp(input->schema, POISON_EXPORT_SCHEMA) == 0 && input->manifest_sha256[0] == '\0' &&
        input->signature[0] == '\0' && input->accepted_evidence_ids == 0u &&
        rpc_poison_export_batch_digest(evidence, batch_sha256);
    (void)session_id;
    (void)role;
    memset(actor_id, 0, sizeof(actor_id));

    const bool exact_retry =
        valid && (evidence->export_active || evidence->export_completed) &&
        strcmp(evidence->export_id, input->export_id) == 0 &&
        strcmp(evidence->export_schema, input->schema) == 0 &&
        evidence->export_last_batch == input->batch_index &&
        evidence->export_last_batch_finalize == input->finalize &&
        memcmp(evidence->export_last_batch_sha256, batch_sha256, sizeof(batch_sha256)) == 0;
    if(exact_retry) {
        memset(batch_sha256, 0, sizeof(batch_sha256));
        rpc_poison_export_send_response(evidence, request, input->finalize);
        return;
    }

    if(valid && input->batch_index == 0u) {
        if(evidence->export_active) rpc_poison_evidence_export_reset(evidence);
        evidence->export_completed = false;
        valid = rpc_poison_export_begin(evidence, input);
    } else if(valid) {
        valid = evidence->export_active && strcmp(evidence->export_id, input->export_id) == 0 &&
                strcmp(evidence->export_schema, input->schema) == 0;
    }
    valid = valid && input->batch_index == evidence->export_next_batch &&
            evidence->export_accepted_count + evidence->decoded_export_count <=
                POISON_EXPORT_EVIDENCE_MAX;
    for(size_t index = 0u; valid && index < evidence->decoded_export_count; ++index) {
        const char* identifier = evidence->decoded_export_ids[index];
        valid = poison_evidence_record_exists_global(identifier) &&
                (index == 0u ? evidence->export_last_evidence_id[0] == '\0' ||
                                   strcmp(evidence->export_last_evidence_id, identifier) < 0 :
                               strcmp(evidence->decoded_export_ids[index - 1u], identifier) < 0);
    }
    if(valid) valid = rpc_poison_export_append(evidence);
    for(size_t index = 0u; valid && index < evidence->decoded_export_count; ++index) {
        valid = mbedtls_sha256_update(
                    &evidence->export_hash,
                    (const uint8_t*)evidence->decoded_export_ids[index],
                    strlen(evidence->decoded_export_ids[index]) + 1u) == 0;
    }
    if(valid) {
        evidence->export_accepted_count += evidence->decoded_export_count;
        evidence->export_last_batch = input->batch_index;
        evidence->export_last_batch_finalize = input->finalize;
        memcpy(evidence->export_last_batch_sha256, batch_sha256, sizeof(batch_sha256));
        strcpy(
            evidence->export_last_evidence_id,
            evidence->decoded_export_ids[evidence->decoded_export_count - 1u]);
        evidence->export_next_batch++;
    }
    if(valid && input->finalize) valid = rpc_poison_export_finalize(evidence);
    memset(batch_sha256, 0, sizeof(batch_sha256));
    if(!valid) {
        if(evidence->export_active) rpc_poison_evidence_export_reset(evidence);
        rpc_send_and_release_empty(
            evidence->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    rpc_poison_export_send_response(evidence, request, input->finalize);
}

void* rpc_system_poison_evidence_alloc(RpcSession* session) {
    RpcPoisonEvidence* evidence = malloc(sizeof(*evidence));
    furi_check(evidence);
    memset(evidence, 0, sizeof(*evidence));
    evidence->session = session;
    RpcHandler handler = {
        .message_handler = rpc_poison_evidence_record_process,
        .decode_submessage = NULL,
        .context = evidence,
    };
    rpc_add_handler(session, PB_Main_poison_evidence_record_tag, &handler);
    handler.message_handler = rpc_poison_case_process;
    rpc_add_handler(session, PB_Main_poison_case_tag, &handler);
    handler.message_handler = rpc_poison_annotation_process;
    rpc_add_handler(session, PB_Main_poison_annotation_tag, &handler);
    handler.message_handler = rpc_poison_export_manifest_process;
    handler.decode_submessage = rpc_poison_evidence_decode_submessage;
    rpc_add_handler(session, PB_Main_poison_export_manifest_tag, &handler);
    return evidence;
}

void rpc_system_poison_evidence_free(void* context) {
    if(!context) return;
    RpcPoisonEvidence* evidence = context;
    rpc_poison_evidence_export_reset(evidence);
    memset(evidence, 0, sizeof(*evidence));
    free(evidence);
}
