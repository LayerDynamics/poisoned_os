#include "rpc_poison_content_update.h"

#include <furi.h>
#include <mbedtls/sha256.h>
#include <storage/storage.h>

#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t magic[4u];
    uint32_t version;
    bool active;
    PoisonContentUpdate update;
    PoisonPackageVerifiedArchive verified_archive;
    char manifest_path[POISON_CONTENT_UPDATE_MANIFEST_PATH_BYTES];
    char activation_manifest_path[POISON_CONTENT_UPDATE_MANIFEST_PATH_BYTES];
    char last_known_good_manifest_path[POISON_CONTENT_UPDATE_MANIFEST_PATH_BYTES];
    char previous_known_good_manifest_path[POISON_CONTENT_UPDATE_MANIFEST_PATH_BYTES];
    char accepted_digest[POISON_CONTENT_UPDATE_MAX_DIGEST];
    uint32_t highest_release_sequence;
    uint8_t digest[32u];
} RpcPoisonContentUpdateStateFile;

#define RPC_POISON_CONTENT_UPDATE_CONFIRMATION_TTL_MS (60000u)

static bool rpc_poison_content_update_token_matches(
    const uint8_t expected[POISON_CONTENT_UPDATE_CONFIRMATION_TOKEN_BYTES],
    const PB_Poison_ContentUpdateRequest_confirmation_token_t* actual) {
    if(actual->size != POISON_CONTENT_UPDATE_CONFIRMATION_TOKEN_BYTES) return false;
    uint8_t difference = 0u;
    for(size_t index = 0; index < POISON_CONTENT_UPDATE_CONFIRMATION_TOKEN_BYTES; ++index) {
        difference |= expected[index] ^ actual->bytes[index];
    }
    return difference == 0u;
}

bool rpc_poison_content_update_confirmation_matches(
    const RpcPoisonContentUpdate* engine,
    const PB_Poison_ContentUpdateRequest* request,
    const RpcPoisonContentUpdateRequestContext* request_context) {
    if(!engine || !request || !request_context || !engine->active ||
       request->operation != PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_ACTIVATE ||
       strcmp(engine->update.update_id, request->update_id) != 0 ||
       !rpc_poison_content_update_token_matches(
           engine->confirmation.token, &request->confirmation_token)) {
        return false;
    }
    PoisonConfirmation probe = engine->confirmation;
    const PoisonConfirmationResult result = poison_confirmation_approve(
        &probe,
        request_context->session_id,
        request_context->role,
        probe.command_digest,
        probe.target_digest,
        probe.consequence_digest,
        request->confirmation_token.bytes,
        request_context->policy_version,
        request_context->now_ms,
        true);
    memset(&probe, 0, sizeof(probe));
    return result == PoisonConfirmationResultOk;
}

static bool rpc_poison_content_update_confirmation_digest(
    const RpcPoisonContentUpdate* engine,
    const char* domain,
    uint8_t digest[POISON_CONFIRMATION_DIGEST_BYTES]) {
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    const uint32_t type = engine->update.content_type;
    const bool ok =
        mbedtls_sha256_starts(&hash, 0) == 0 &&
        mbedtls_sha256_update(&hash, (const uint8_t*)domain, strlen(domain)) == 0 &&
        mbedtls_sha256_update(
            &hash, (const uint8_t*)engine->update.update_id, strlen(engine->update.update_id)) ==
            0 &&
        mbedtls_sha256_update(
            &hash,
            (const uint8_t*)engine->update.candidate_digest,
            strlen(engine->update.candidate_digest)) == 0 &&
        mbedtls_sha256_update(
            &hash,
            (const uint8_t*)engine->update.previous_digest,
            strlen(engine->update.previous_digest)) == 0 &&
        mbedtls_sha256_update(&hash, (const uint8_t*)&engine->update.sequence, sizeof(uint32_t)) ==
            0 &&
        mbedtls_sha256_update(&hash, (const uint8_t*)&type, sizeof(type)) == 0 &&
        mbedtls_sha256_finish(&hash, digest) == 0;
    mbedtls_sha256_free(&hash);
    return ok;
}

static bool rpc_poison_content_update_quarantine(PoisonContentUpdate* update) {
    if(!update || update->state == PoisonContentUpdateHealthy ||
       update->state == PoisonContentUpdateRolledBack ||
       update->state == PoisonContentUpdateQuarantined) {
        return false;
    }
    update->state = PoisonContentUpdateQuarantined;
    return true;
}

static bool rpc_poison_content_update_id_matches(
    const RpcPoisonContentUpdate* engine,
    const char* update_id) {
    return engine->active && update_id && strcmp(engine->update.update_id, update_id) == 0;
}

static bool rpc_poison_content_update_manifest_path_valid(const char* manifest_path) {
    if(!manifest_path || strncmp(manifest_path, "/ext/update/", 12u) != 0 ||
       strstr(manifest_path, "..")) {
        return false;
    }
    const size_t length = strnlen(manifest_path, POISON_CONTENT_UPDATE_MANIFEST_PATH_BYTES);
    static const char suffix[] = ".poison";
    return length > sizeof(suffix) - 1u && length < POISON_CONTENT_UPDATE_MANIFEST_PATH_BYTES &&
           strcmp(manifest_path + length - (sizeof(suffix) - 1u), suffix) == 0;
}

static bool rpc_poison_content_update_fuf_path_valid(const char* manifest_path) {
    if(!manifest_path || strncmp(manifest_path, "/ext/update/", 12u) != 0 ||
       strstr(manifest_path, "..")) {
        return false;
    }
    const size_t length = strnlen(manifest_path, POISON_CONTENT_UPDATE_MANIFEST_PATH_BYTES);
    static const char suffix[] = "/update.fuf";
    return length > sizeof(suffix) - 1u && length < POISON_CONTENT_UPDATE_MANIFEST_PATH_BYTES &&
           strcmp(manifest_path + length - (sizeof(suffix) - 1u), suffix) == 0;
}

static bool
    rpc_poison_content_update_type(const char* content_type, PoisonContentUpdateType* type) {
    static const char* const names[] = {
        "application",
        "firmware",
        "lesson",
        "tool-data",
        "theme",
        "font-icon",
        "menu",
        "resource",
        "ui-pack",
    };
    if(!content_type || !type) return false;
    if(strcmp(content_type, "font") == 0 || strcmp(content_type, "icon") == 0) {
        *type = PoisonContentUpdateFontIcon;
        return true;
    }
    for(size_t index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        if(strcmp(content_type, names[index]) == 0) {
            *type = (PoisonContentUpdateType)index;
            return true;
        }
    }
    return false;
}

static bool rpc_poison_content_update_requires_confirmation(PoisonContentUpdateType type) {
    return type == PoisonContentUpdateFirmware || type == PoisonContentUpdateResource ||
           type == PoisonContentUpdateUiPack;
}

static void rpc_poison_content_update_fill_status(
    const RpcPoisonContentUpdate* engine,
    const char* result,
    PB_Poison_ContentUpdateStatus* status) {
    *status = (PB_Poison_ContentUpdateStatus)PB_Poison_ContentUpdateStatus_init_zero;
    memcpy(status->update_id, engine->update.update_id, strlen(engine->update.update_id) + 1u);
    status->state = (PB_Poison_ContentUpdateState)engine->update.state;
    status->content_type = (PB_Poison_ContentUpdateType)engine->update.content_type;
    memcpy(
        status->candidate_digest,
        engine->update.candidate_digest,
        strlen(engine->update.candidate_digest) + 1u);
    memcpy(
        status->previous_digest,
        engine->update.previous_digest,
        strlen(engine->update.previous_digest) + 1u);
    status->release_sequence = engine->update.sequence;
    status->received_bytes = engine->update.received_bytes;
    status->content_bytes = engine->update.content_bytes;
    status->confirmation_required = engine->update.confirmation_required;
    memcpy(status->result, result, strlen(result) + 1u);
    if(engine->confirmation.active && !engine->confirmation.consumed) {
        status->confirmation_token.size = sizeof(engine->confirmation.token);
        memcpy(
            status->confirmation_token.bytes,
            engine->confirmation.token,
            sizeof(engine->confirmation.token));
    }
}

void rpc_poison_content_update_init(RpcPoisonContentUpdate* engine) {
    if(engine) {
        memset(engine, 0, sizeof(*engine));
        strcpy(
            engine->accepted_digest,
            "0000000000000000000000000000000000000000000000000000000000000000");
    }
}

void rpc_poison_content_update_set_environment(
    RpcPoisonContentUpdate* engine,
    uint32_t hardware_target,
    uint32_t firmware_api,
    uint32_t highest_release_sequence,
    uint64_t available_storage_bytes) {
    if(!engine) return;
    engine->hardware_target = hardware_target;
    engine->firmware_api = firmware_api;
    engine->highest_release_sequence = highest_release_sequence;
    engine->available_storage_bytes = available_storage_bytes;
}

void rpc_poison_content_update_set_activation_callback(
    RpcPoisonContentUpdate* engine,
    RpcPoisonContentUpdateActivationCallback callback,
    void* context) {
    if(!engine) return;
    engine->activation_callback = callback;
    engine->activation_context = context;
}

void rpc_poison_content_update_set_rollback_callback(
    RpcPoisonContentUpdate* engine,
    RpcPoisonContentUpdateRollbackCallback callback,
    void* context) {
    if(!engine) return;
    engine->rollback_callback = callback;
    engine->rollback_context = context;
}

void rpc_poison_content_update_set_verification_callback(
    RpcPoisonContentUpdate* engine,
    RpcPoisonContentUpdateVerificationCallback callback,
    void* context) {
    if(!engine) return;
    engine->verification_callback = callback;
    engine->verification_context = context;
}

bool rpc_poison_content_update_set_last_known_good(
    RpcPoisonContentUpdate* engine,
    const char* manifest_path) {
    if(!engine || !rpc_poison_content_update_fuf_path_valid(manifest_path)) return false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    const bool exists = storage_common_stat(storage, manifest_path, NULL) == FSE_OK;
    furi_record_close(RECORD_STORAGE);
    if(!exists) return false;
    strcpy(engine->last_known_good_manifest_path, manifest_path);
    return true;
}

bool rpc_poison_content_update_promote_last_known_good(
    RpcPoisonContentUpdate* engine,
    const char* manifest_path) {
    if(!engine) return false;
    char previous[POISON_CONTENT_UPDATE_MANIFEST_PATH_BYTES];
    strcpy(previous, engine->last_known_good_manifest_path);
    if(!rpc_poison_content_update_set_last_known_good(engine, manifest_path)) return false;
    strcpy(engine->previous_known_good_manifest_path, previous);
    memset(previous, 0, sizeof(previous));
    return true;
}

static bool rpc_poison_content_update_import(
    RpcPoisonContentUpdate* engine,
    const PB_Poison_ContentUpdateRequest* request) {
    PoisonPackageVerifiedArchive* verified = malloc(sizeof(*verified));
    if(!verified) return false;
    memset(verified, 0, sizeof(*verified));
    PoisonContentUpdateType signed_type;
    const bool callback_valid = engine->verification_callback && engine->verification_callback(
                                                                     engine->verification_context,
                                                                     request->manifest_path,
                                                                     request->candidate_digest,
                                                                     verified);
    const bool verified_valid =
        callback_valid && rpc_poison_content_update_type(verified->content_type, &signed_type) &&
        strcmp(verified->archive_sha256, request->candidate_digest) == 0 &&
        (signed_type != PoisonContentUpdateFirmware ||
         strcmp(verified->entrypoint, "update.fuf") == 0) &&
        rpc_poison_content_update_manifest_path_valid(request->manifest_path) &&
        engine->hardware_target != 0u && engine->firmware_api != 0u &&
        engine->available_storage_bytes != 0u && engine->available_storage_bytes <= UINT32_MAX;
    if(!verified_valid) {
        memset(verified, 0, sizeof(*verified));
        free(verified);
        return false;
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    const bool rollback_available =
        rpc_poison_content_update_fuf_path_valid(engine->last_known_good_manifest_path) &&
        storage_common_stat(storage, engine->last_known_good_manifest_path, NULL) == FSE_OK;
    furi_record_close(RECORD_STORAGE);
    const PoisonContentUpdateManifest manifest = {
        .content_type = signed_type,
        .update_id = verified->package_id,
        .candidate_digest = verified->archive_sha256,
        .previous_digest = engine->accepted_digest,
        .hardware_target = engine->hardware_target,
        .minimum_api = engine->firmware_api,
        .maximum_api = engine->firmware_api,
        .release_sequence = verified->release_sequence,
        .content_bytes = verified->archive_bytes,
        .signature_valid = true,
        .rollback_available = rollback_available,
        .protected_target = rpc_poison_content_update_requires_confirmation(signed_type),
    };
    const PoisonContentUpdateEnvironment environment = {
        .hardware_target = engine->hardware_target,
        .firmware_api = engine->firmware_api,
        .highest_release_sequence = engine->highest_release_sequence,
        .available_storage_bytes = (uint32_t)engine->available_storage_bytes,
    };
    PoisonContentUpdate admitted;
    if(poison_content_update_admit(&admitted, &manifest, &environment) !=
       PoisonContentUpdateAdmissionOk) {
        memset(verified, 0, sizeof(*verified));
        free(verified);
        return false;
    }
    engine->update = admitted;
    memcpy(engine->manifest_path, request->manifest_path, strlen(request->manifest_path) + 1u);
    engine->verified_archive = *verified;
    engine->activation_manifest_path[0] = '\0';
    engine->active = true;
    memset(&engine->confirmation, 0, sizeof(engine->confirmation));
    memset(verified, 0, sizeof(*verified));
    free(verified);
    return true;
}

static bool rpc_poison_content_update_stage(
    RpcPoisonContentUpdate* engine,
    const PB_Poison_ContentUpdateRequest* request) {
    if(engine->update.state == PoisonContentUpdateDiscovered &&
       !poison_content_update_transition(&engine->update, PoisonContentUpdateReceiving)) {
        return false;
    }
    if(!poison_content_update_receive(&engine->update, request->received_bytes)) return false;
    if(engine->update.received_bytes == engine->update.content_bytes) {
        return poison_content_update_transition(&engine->update, PoisonContentUpdateStaged);
    }
    return true;
}

static bool rpc_poison_content_update_verify(
    RpcPoisonContentUpdate* engine,
    const PB_Poison_ContentUpdateRequest* request,
    const RpcPoisonContentUpdateRequestContext* request_context) {
    PoisonPackageVerifiedArchive* verified = malloc(sizeof(*verified));
    if(!verified) return false;
    memset(verified, 0, sizeof(*verified));
    const bool verified_valid =
        engine->verification_callback &&
        engine->verification_callback(
            engine->verification_context,
            engine->manifest_path,
            request->candidate_digest,
            verified) &&
        strcmp(verified->content_type, engine->verified_archive.content_type) == 0 &&
        strcmp(verified->package_id, engine->verified_archive.package_id) == 0 &&
        strcmp(verified->version, engine->verified_archive.version) == 0 &&
        strcmp(verified->entrypoint, engine->verified_archive.entrypoint) == 0 &&
        strcmp(verified->archive_sha256, engine->verified_archive.archive_sha256) == 0 &&
        verified->release_sequence == engine->verified_archive.release_sequence &&
        verified->archive_bytes == engine->verified_archive.archive_bytes &&
        poison_content_update_verify_payload(&engine->update, request->candidate_digest) &&
        poison_content_update_transition(&engine->update, PoisonContentUpdateAwaitingConfirmation);
    memset(verified, 0, sizeof(*verified));
    free(verified);
    if(!verified_valid) {
        return false;
    }
    if(engine->update.confirmation_required) {
        uint8_t command_digest[POISON_CONFIRMATION_DIGEST_BYTES];
        uint8_t target_digest[POISON_CONFIRMATION_DIGEST_BYTES];
        uint8_t consequence_digest[POISON_CONFIRMATION_DIGEST_BYTES];
        const bool issued = request_context && request_context->session_id != 0u &&
                            request_context->role < PoisonRoleCount &&
                            rpc_poison_content_update_confirmation_digest(
                                engine, "update.activate.command", command_digest) &&
                            rpc_poison_content_update_confirmation_digest(
                                engine, "update.activate.target", target_digest) &&
                            rpc_poison_content_update_confirmation_digest(
                                engine, "update.activate.consequence", consequence_digest) &&
                            poison_confirmation_issue(
                                &engine->confirmation,
                                request_context->session_id,
                                request_context->role,
                                command_digest,
                                target_digest,
                                consequence_digest,
                                request_context->policy_version,
                                request_context->now_ms,
                                RPC_POISON_CONTENT_UPDATE_CONFIRMATION_TTL_MS,
                                true) == PoisonConfirmationResultOk;
        memset(command_digest, 0, sizeof(command_digest));
        memset(target_digest, 0, sizeof(target_digest));
        memset(consequence_digest, 0, sizeof(consequence_digest));
        if(!issued) {
            engine->update.state = PoisonContentUpdateStaged;
            engine->update.payload_verified = false;
            return false;
        }
    }
    return true;
}

static bool rpc_poison_content_update_activate(
    RpcPoisonContentUpdate* engine,
    const PB_Poison_ContentUpdateRequest* request,
    const RpcPoisonContentUpdateRequestContext* request_context) {
    bool authorized = !engine->update.confirmation_required;
    if(engine->update.confirmation_required) {
        authorized =
            request_context &&
            rpc_poison_content_update_confirmation_matches(engine, request, request_context) &&
            poison_confirmation_approve(
                &engine->confirmation,
                request_context->session_id,
                request_context->role,
                engine->confirmation.command_digest,
                engine->confirmation.target_digest,
                engine->confirmation.consequence_digest,
                request->confirmation_token.bytes,
                request_context->policy_version,
                request_context->now_ms,
                request_context->physical_confirmed) == PoisonConfirmationResultOk;
    }
    if(!authorized || !poison_content_update_confirm(&engine->update, authorized) ||
       !engine->activation_callback ||
       !engine->activation_callback(
           engine->activation_context,
           engine->activation_manifest_path[0] ? engine->activation_manifest_path :
                                                 engine->manifest_path) ||
       !poison_content_update_transition(&engine->update, PoisonContentUpdateActivating)) {
        return false;
    }
    memset(&engine->confirmation, 0, sizeof(engine->confirmation));
    return true;
}

bool rpc_poison_content_update_process(
    RpcPoisonContentUpdate* engine,
    const PB_Poison_ContentUpdateRequest* request,
    const RpcPoisonContentUpdateRequestContext* request_context,
    PB_Poison_ContentUpdateStatus* status) {
    if(!engine || !request || !status) return false;
    bool result = false;
    const char* result_name = "invalid";
    if(request->operation == PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_IMPORT) {
        result = rpc_poison_content_update_import(engine, request);
        result_name = "imported";
    } else if(!rpc_poison_content_update_id_matches(engine, request->update_id)) {
        return false;
    } else {
        switch(request->operation) {
        case PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_INSPECT:
            result = !request->healthy;
            result_name = "inspected";
            break;
        case PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_STAGE:
            result = rpc_poison_content_update_stage(engine, request);
            result_name = engine->update.state == PoisonContentUpdateStaged ? "staged" :
                                                                              "receiving";
            break;
        case PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_VERIFY:
            result = rpc_poison_content_update_verify(engine, request, request_context);
            result_name = "verified";
            break;
        case PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_ACTIVATE:
            result = rpc_poison_content_update_activate(engine, request, request_context);
            result_name = "activating";
            break;
        case PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_HEALTH:
            result = true;
            result_name = engine->update.state == PoisonContentUpdateHealthy    ? "healthy" :
                          engine->update.state == PoisonContentUpdateRolledBack ? "rolled-back" :
                                                                                  "pending-health";
            break;
        case PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_CANCEL:
            result = poison_content_update_cancel(&engine->update);
            result_name = "cancelled";
            break;
        case PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_ROLLBACK: {
            const bool rollback_to_previous = engine->update.state == PoisonContentUpdateHealthy;
            const char* rollback_path = rollback_to_previous ?
                                            engine->previous_known_good_manifest_path :
                                            engine->last_known_good_manifest_path;
            result = rollback_path[0] != '\0' && engine->rollback_callback &&
                     engine->rollback_callback(engine->rollback_context, rollback_path) &&
                     poison_content_update_rollback(&engine->update);
            if(result && rollback_to_previous) {
                char current[POISON_CONTENT_UPDATE_MANIFEST_PATH_BYTES];
                strcpy(current, engine->last_known_good_manifest_path);
                strcpy(engine->last_known_good_manifest_path, rollback_path);
                strcpy(engine->previous_known_good_manifest_path, current);
                memset(current, 0, sizeof(current));
            }
            if(result) strcpy(engine->accepted_digest, engine->update.previous_digest);
        }
            result_name = "rolled-back";
            break;
        case PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_QUARANTINE:
            result = rpc_poison_content_update_quarantine(&engine->update);
            result_name = "quarantined";
            break;
        default:
            return false;
        }
    }
    if(!result) return false;
    rpc_poison_content_update_fill_status(engine, result_name, status);
    return true;
}

static bool rpc_poison_content_update_digest_valid(const char* value) {
    if(!value || strlen(value) != 64u) return false;
    for(size_t index = 0; index < 64u; ++index) {
        if(!((value[index] >= '0' && value[index] <= '9') ||
             (value[index] >= 'a' && value[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

static bool rpc_poison_content_update_state_digest(
    const RpcPoisonContentUpdateStateFile* state,
    uint8_t digest[32u]) {
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool ok =
        mbedtls_sha256_starts(&hash, 0) == 0 &&
        mbedtls_sha256_update(
            &hash, (const uint8_t*)state, offsetof(RpcPoisonContentUpdateStateFile, digest)) ==
            0 &&
        mbedtls_sha256_finish(&hash, digest) == 0;
    mbedtls_sha256_free(&hash);
    return ok;
}

static bool
    rpc_poison_content_update_persisted_valid(const RpcPoisonContentUpdateStateFile* state) {
    if(!state || memcmp(state->magic, "PCUS", 4u) != 0 || state->version != 2u ||
       !rpc_poison_content_update_digest_valid(state->accepted_digest) ||
       (state->activation_manifest_path[0] != '\0' &&
        !rpc_poison_content_update_fuf_path_valid(state->activation_manifest_path)) ||
       (state->last_known_good_manifest_path[0] != '\0' &&
        !rpc_poison_content_update_fuf_path_valid(state->last_known_good_manifest_path)) ||
       (state->previous_known_good_manifest_path[0] != '\0' &&
        !rpc_poison_content_update_fuf_path_valid(state->previous_known_good_manifest_path))) {
        return false;
    }
    if(!state->active) return true;
    return state->update.state <= PoisonContentUpdateQuarantined &&
           state->update.content_type < PoisonContentUpdateTypeCount &&
           state->update.sequence > 0u && state->update.content_bytes > 0u &&
           state->update.received_bytes <= state->update.content_bytes &&
           strcmp(state->update.update_id, state->verified_archive.package_id) == 0 &&
           strcmp(state->update.candidate_digest, state->verified_archive.archive_sha256) == 0 &&
           state->update.sequence == state->verified_archive.release_sequence &&
           state->update.content_bytes == state->verified_archive.archive_bytes &&
           rpc_poison_content_update_digest_valid(state->update.candidate_digest) &&
           rpc_poison_content_update_digest_valid(state->update.previous_digest) &&
           rpc_poison_content_update_manifest_path_valid(state->manifest_path) &&
           state->verified_archive.payload_count > 0u &&
           state->verified_archive.payload_count <= POISON_PACKAGE_PAYLOAD_MAX;
}

bool rpc_poison_content_update_save(const RpcPoisonContentUpdate* engine, const char* state_path) {
    if(!engine || !state_path || state_path[0] != '/' || strstr(state_path, "..")) return false;
    RpcPoisonContentUpdateStateFile* state = malloc(sizeof(*state));
    if(!state) return false;
    memset(state, 0, sizeof(*state));
    memcpy(state->magic, "PCUS", 4u);
    state->version = 2u;
    state->active = engine->active;
    state->update = engine->update;
    state->verified_archive = engine->verified_archive;
    strcpy(state->manifest_path, engine->manifest_path);
    strcpy(state->activation_manifest_path, engine->activation_manifest_path);
    strcpy(state->last_known_good_manifest_path, engine->last_known_good_manifest_path);
    strcpy(state->previous_known_good_manifest_path, engine->previous_known_good_manifest_path);
    strcpy(state->accepted_digest, engine->accepted_digest);
    state->highest_release_sequence = engine->highest_release_sequence;
    bool ok = rpc_poison_content_update_persisted_valid(state) &&
              rpc_poison_content_update_state_digest(state, state->digest);
    char partial[POISON_CONTENT_UPDATE_MANIFEST_PATH_BYTES];
    if(snprintf(partial, sizeof(partial), "%s.partial", state_path) >= (int)sizeof(partial))
        ok = false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(ok) ok = storage_simply_mkdir(storage, "/int/.poison");
    File* file = storage_file_alloc(storage);
    if(ok) ok = storage_file_open(file, partial, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) ok = storage_file_write(file, state, sizeof(*state)) == sizeof(*state);
    if(ok) ok = storage_file_sync(file);
    if(storage_file_is_open(file)) ok = storage_file_close(file) && ok;
    if(ok) ok = storage_common_rename(storage, partial, state_path) == FSE_OK;
    storage_file_free(file);
    if(!ok) (void)storage_simply_remove(storage, partial);
    furi_record_close(RECORD_STORAGE);
    memset(state, 0, sizeof(*state));
    free(state);
    return ok;
}

bool rpc_poison_content_update_load(RpcPoisonContentUpdate* engine, const char* state_path) {
    if(!engine || !state_path || state_path[0] != '/' || strstr(state_path, "..")) return false;
    RpcPoisonContentUpdateStateFile* state = malloc(sizeof(*state));
    if(!state) return false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, state_path, FSAM_READ, FSOM_OPEN_EXISTING) &&
              storage_file_size(file) == sizeof(*state) &&
              storage_file_read(file, state, sizeof(*state)) == sizeof(*state) &&
              !storage_file_get_error(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    uint8_t digest[32u];
    ok = ok && rpc_poison_content_update_persisted_valid(state) &&
         rpc_poison_content_update_state_digest(state, digest) &&
         memcmp(digest, state->digest, sizeof(digest)) == 0;
    if(ok) {
        engine->active = state->active;
        engine->update = state->update;
        engine->verified_archive = state->verified_archive;
        strcpy(engine->manifest_path, state->manifest_path);
        strcpy(engine->activation_manifest_path, state->activation_manifest_path);
        strcpy(engine->last_known_good_manifest_path, state->last_known_good_manifest_path);
        strcpy(
            engine->previous_known_good_manifest_path, state->previous_known_good_manifest_path);
        strcpy(engine->accepted_digest, state->accepted_digest);
        engine->highest_release_sequence = state->highest_release_sequence;
    }
    memset(digest, 0, sizeof(digest));
    memset(state, 0, sizeof(*state));
    free(state);
    return ok;
}
