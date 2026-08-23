#include "poison_content_update_internal.h"

#include <mbedtls/sha256.h>
#include <storage/storage.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t magic[4u];
    uint32_t version;
    char update_id[POISON_CONTENT_UPDATE_MAX_ID];
    char candidate_digest[POISON_CONTENT_UPDATE_MAX_DIGEST];
    uint32_t sequence;
    uint8_t checksum[32u];
} PoisonContentUpdateHealthMarker;

static bool bounded_digest(const char* value);
static bool bounded_id(const char* value);

static bool poison_content_update_marker_path_valid(const char* path) {
    return path && path[0] == '/' && !strstr(path, "..") && strlen(path) < 224u;
}

static bool poison_content_update_marker_checksum(
    const PoisonContentUpdateHealthMarker* marker,
    uint8_t checksum[32u]) {
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    const bool ok =
        mbedtls_sha256_starts(&hash, 0) == 0 &&
        mbedtls_sha256_update(
            &hash, (const uint8_t*)marker, offsetof(PoisonContentUpdateHealthMarker, checksum)) ==
            0 &&
        mbedtls_sha256_finish(&hash, checksum) == 0;
    mbedtls_sha256_free(&hash);
    return ok;
}

static bool poison_content_update_marker_valid(
    const PoisonContentUpdateHealthMarker* marker,
    const uint8_t expected_magic[4u]) {
    uint8_t checksum[32u];
    const bool valid = marker && memcmp(marker->magic, expected_magic, 4u) == 0 &&
                       marker->version == 1u && bounded_id(marker->update_id) &&
                       bounded_digest(marker->candidate_digest) && marker->sequence > 0u &&
                       poison_content_update_marker_checksum(marker, checksum) &&
                       memcmp(checksum, marker->checksum, sizeof(checksum)) == 0;
    memset(checksum, 0, sizeof(checksum));
    return valid;
}

static bool poison_content_update_marker_read(
    const char* path,
    const uint8_t expected_magic[4u],
    PoisonContentUpdateHealthMarker* marker) {
    if(!poison_content_update_marker_path_valid(path) || !marker) return false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    const bool read = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
                      storage_file_size(file) == sizeof(*marker) &&
                      storage_file_read(file, marker, sizeof(*marker)) == sizeof(*marker) &&
                      !storage_file_get_error(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return read && poison_content_update_marker_valid(marker, expected_magic);
}

static bool
    poison_content_update_marker_write(const char* path, PoisonContentUpdateHealthMarker* marker) {
    if(!poison_content_update_marker_path_valid(path) || !marker ||
       !poison_content_update_marker_checksum(marker, marker->checksum)) {
        return false;
    }
    char partial[256u];
    if(snprintf(partial, sizeof(partial), "%s.partial", path) >= (int)sizeof(partial)) {
        return false;
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool written = storage_file_open(file, partial, FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
                   storage_file_write(file, marker, sizeof(*marker)) == sizeof(*marker) &&
                   storage_file_sync(file);
    if(storage_file_is_open(file)) written = storage_file_close(file) && written;
    if(written) {
        (void)storage_simply_remove(storage, path);
        written = storage_common_rename(storage, partial, path) == FSE_OK;
    }
    if(!written) (void)storage_simply_remove(storage, partial);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return written;
}

static bool bounded_digest(const char* value) {
    if(!value || strlen(value) != POISON_CONTENT_UPDATE_MAX_DIGEST - 1u) return false;
    for(size_t i = 0; i < POISON_CONTENT_UPDATE_MAX_DIGEST - 1u; i++) {
        const char c = value[i];
        if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static bool bounded_id(const char* value) {
    if(!value || value[0] == '\0' || strlen(value) >= POISON_CONTENT_UPDATE_MAX_ID) return false;
    if(!((value[0] >= 'a' && value[0] <= 'z') || (value[0] >= '0' && value[0] <= '9')))
        return false;
    for(const char* cursor = value + 1; *cursor; cursor++) {
        if(!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= '0' && *cursor <= '9') ||
             *cursor == '.' || *cursor == '-' || *cursor == '_'))
            return false;
    }
    return true;
}

static bool poison_content_update_requires_confirmation(
    PoisonContentUpdateType content_type,
    bool protected_target) {
    return protected_target || content_type == PoisonContentUpdateFirmware ||
           content_type == PoisonContentUpdateResource;
}

PoisonContentUpdateAdmission poison_content_update_admit(
    PoisonContentUpdate* update,
    const PoisonContentUpdateManifest* manifest,
    const PoisonContentUpdateEnvironment* environment) {
    if(!update || !manifest || !environment ||
       manifest->content_type >= PoisonContentUpdateTypeCount || manifest->hardware_target == 0u ||
       manifest->minimum_api == 0u || manifest->maximum_api < manifest->minimum_api ||
       manifest->content_bytes == 0u || environment->hardware_target == 0u ||
       environment->firmware_api == 0u || !bounded_id(manifest->update_id) ||
       !bounded_digest(manifest->candidate_digest) || !bounded_digest(manifest->previous_digest) ||
       strcmp(manifest->candidate_digest, manifest->previous_digest) == 0) {
        return PoisonContentUpdateAdmissionInvalid;
    }
    if(!manifest->signature_valid) return PoisonContentUpdateAdmissionTampered;
    if(manifest->signer_revoked) return PoisonContentUpdateAdmissionRevokedSigner;
    if(manifest->hardware_target != environment->hardware_target)
        return PoisonContentUpdateAdmissionWrongTarget;
    if(environment->firmware_api < manifest->minimum_api ||
       environment->firmware_api > manifest->maximum_api)
        return PoisonContentUpdateAdmissionIncompatibleApi;
    if(manifest->release_sequence <= environment->highest_release_sequence)
        return PoisonContentUpdateAdmissionDowngrade;
    if(manifest->content_bytes > environment->available_storage_bytes)
        return PoisonContentUpdateAdmissionInsufficientStorage;
    if(!manifest->rollback_available) return PoisonContentUpdateAdmissionMissingRollback;
    if(!poison_content_update_begin(
           update,
           manifest->update_id,
           manifest->candidate_digest,
           manifest->previous_digest,
           manifest->release_sequence)) {
        return PoisonContentUpdateAdmissionInvalid;
    }
    update->content_type = manifest->content_type;
    update->content_bytes = manifest->content_bytes;
    update->rollback_available = manifest->rollback_available;
    update->confirmation_required = poison_content_update_requires_confirmation(
        manifest->content_type, manifest->protected_target);
    return PoisonContentUpdateAdmissionOk;
}

bool poison_content_update_begin(
    PoisonContentUpdate* update,
    const char* update_id,
    const char* candidate_digest,
    const char* previous_digest,
    uint32_t sequence) {
    if(!update || !bounded_id(update_id) || !bounded_digest(candidate_digest) ||
       !bounded_digest(previous_digest) || sequence == 0u)
        return false;
    memset(update, 0, sizeof(*update));
    strncpy(update->update_id, update_id, sizeof(update->update_id) - 1u);
    strncpy(update->candidate_digest, candidate_digest, sizeof(update->candidate_digest) - 1u);
    strncpy(update->previous_digest, previous_digest, sizeof(update->previous_digest) - 1u);
    update->state = PoisonContentUpdateDiscovered;
    update->sequence = sequence;
    update->rollback_available = true;
    return true;
}

bool poison_content_update_transition(PoisonContentUpdate* update, PoisonContentUpdateState next) {
    if(!update) return false;
    const PoisonContentUpdateState current = update->state;
    const bool allowed =
        (current == PoisonContentUpdateDiscovered && next == PoisonContentUpdateReceiving) ||
        (current == PoisonContentUpdateReceiving &&
         ((next == PoisonContentUpdateStaged &&
           (update->content_bytes == 0u || update->received_bytes == update->content_bytes)) ||
          next == PoisonContentUpdateQuarantined)) ||
        (current == PoisonContentUpdateStaged &&
         ((next == PoisonContentUpdateVerified &&
           (update->content_bytes == 0u || update->payload_verified)) ||
          next == PoisonContentUpdateQuarantined)) ||
        (current == PoisonContentUpdateVerified &&
         next == PoisonContentUpdateAwaitingConfirmation) ||
        (current == PoisonContentUpdateAwaitingConfirmation &&
         next == PoisonContentUpdateActivating && poison_content_update_can_activate(update)) ||
        (current == PoisonContentUpdateActivating &&
         ((next == PoisonContentUpdateHealthy && update->health_reported) ||
          next == PoisonContentUpdateRolledBack));
    if(!allowed) return false;
    update->state = next;
    return true;
}

bool poison_content_update_receive(PoisonContentUpdate* update, uint32_t bytes) {
    if(!update || update->state != PoisonContentUpdateReceiving || bytes == 0u ||
       update->content_bytes == 0u || update->received_bytes > update->content_bytes ||
       bytes > update->content_bytes - update->received_bytes) {
        return false;
    }
    update->received_bytes += bytes;
    return true;
}

bool poison_content_update_verify_payload(PoisonContentUpdate* update, const char* actual_digest) {
    if(!update || update->state != PoisonContentUpdateStaged || !bounded_digest(actual_digest))
        return false;
    if(strcmp(update->candidate_digest, actual_digest) != 0) {
        poison_content_update_transition(update, PoisonContentUpdateQuarantined);
        return false;
    }
    update->payload_verified = true;
    return poison_content_update_transition(update, PoisonContentUpdateVerified);
}

bool poison_content_update_confirm(PoisonContentUpdate* update, bool exact_confirmation) {
    if(!update || update->state != PoisonContentUpdateAwaitingConfirmation ||
       (update->confirmation_required && !exact_confirmation)) {
        return false;
    }
    update->confirmation_authorized = true;
    return true;
}

bool poison_content_update_can_activate(const PoisonContentUpdate* update) {
    return update && update->state == PoisonContentUpdateAwaitingConfirmation &&
           update->candidate_digest[0] != '\0' && update->rollback_available &&
           (!update->confirmation_required || update->confirmation_authorized);
}

bool poison_content_update_report_health(PoisonContentUpdate* update, bool healthy) {
    if(!update || update->state != PoisonContentUpdateActivating) return false;
    update->health_reported = true;
    if(healthy) return poison_content_update_transition(update, PoisonContentUpdateHealthy);
    return poison_content_update_rollback(update);
}

bool poison_content_update_recover(PoisonContentUpdate* update, bool rollback_artifact_valid) {
    if(!update || update->state < PoisonContentUpdateReceiving ||
       update->state > PoisonContentUpdateActivating) {
        return false;
    }
    if(!update->rollback_available || !rollback_artifact_valid) {
        update->state = PoisonContentUpdateQuarantined;
        return false;
    }
    update->state = PoisonContentUpdateRolledBack;
    return true;
}

bool poison_content_update_cancel(PoisonContentUpdate* update) {
    if(!update || update->state > PoisonContentUpdateAwaitingConfirmation) return false;
    if(!update->rollback_available) {
        update->state = PoisonContentUpdateQuarantined;
        return false;
    }
    update->state = PoisonContentUpdateRolledBack;
    return true;
}

bool poison_content_update_rollback(PoisonContentUpdate* update) {
    if(!update || !update->rollback_available || update->previous_digest[0] == '\0') return false;
    if(update->state != PoisonContentUpdateActivating &&
       update->state != PoisonContentUpdateHealthy)
        return false;
    update->state = PoisonContentUpdateRolledBack;
    return true;
}

bool poison_content_update_health_arm_at(
    const PoisonContentUpdate* update,
    const char* pending_path) {
    if(!update || update->state != PoisonContentUpdateAwaitingConfirmation ||
       !update->confirmation_authorized || !bounded_id(update->update_id) ||
       !bounded_digest(update->candidate_digest) || update->sequence == 0u) {
        return false;
    }
    PoisonContentUpdateHealthMarker marker = {
        .magic = {'P', 'C', 'U', 'P'},
        .version = 1u,
        .sequence = update->sequence,
    };
    strcpy(marker.update_id, update->update_id);
    strcpy(marker.candidate_digest, update->candidate_digest);
    const bool written = poison_content_update_marker_write(pending_path, &marker);
    memset(&marker, 0, sizeof(marker));
    return written;
}

bool poison_content_update_health_mark_complete_at(
    const char* pending_path,
    const char* complete_path) {
    if(!poison_content_update_marker_path_valid(pending_path) ||
       !poison_content_update_marker_path_valid(complete_path)) {
        return false;
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    const FS_Error pending_status = storage_common_stat(storage, pending_path, NULL);
    furi_record_close(RECORD_STORAGE);
    if(pending_status == FSE_NOT_EXIST) return true;
    if(pending_status != FSE_OK) return false;

    static const uint8_t pending_magic[4u] = {'P', 'C', 'U', 'P'};
    PoisonContentUpdateHealthMarker marker;
    memset(&marker, 0, sizeof(marker));
    if(!poison_content_update_marker_read(pending_path, pending_magic, &marker)) {
        memset(&marker, 0, sizeof(marker));
        return false;
    }
    memcpy(marker.magic, "PCUH", 4u);
    const bool written = poison_content_update_marker_write(complete_path, &marker);
    if(written) {
        storage = furi_record_open(RECORD_STORAGE);
        (void)storage_simply_remove(storage, pending_path);
        furi_record_close(RECORD_STORAGE);
    }
    memset(&marker, 0, sizeof(marker));
    return written;
}

bool poison_content_update_health_completed_at(
    const PoisonContentUpdate* update,
    const char* complete_path) {
    static const uint8_t complete_magic[4u] = {'P', 'C', 'U', 'H'};
    PoisonContentUpdateHealthMarker marker;
    memset(&marker, 0, sizeof(marker));
    const bool completed =
        update && update->state == PoisonContentUpdateActivating &&
        poison_content_update_marker_read(complete_path, complete_magic, &marker) &&
        strcmp(marker.update_id, update->update_id) == 0 &&
        strcmp(marker.candidate_digest, update->candidate_digest) == 0 &&
        marker.sequence == update->sequence;
    memset(&marker, 0, sizeof(marker));
    return completed;
}

void poison_content_update_health_clear_at(const char* pending_path, const char* complete_path) {
    if(!poison_content_update_marker_path_valid(pending_path) ||
       !poison_content_update_marker_path_valid(complete_path)) {
        return;
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    (void)storage_simply_remove(storage, pending_path);
    (void)storage_simply_remove(storage, complete_path);
    furi_record_close(RECORD_STORAGE);
}
