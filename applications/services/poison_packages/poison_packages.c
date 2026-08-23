#include "poison_packages.h"
#include "poison_package_manager.h"

#include <furi.h>
#include <applications/services/poison_startup.h>
#include <storage/storage.h>

#include <mbedtls/sha256.h>

#include <ctype.h>
#include <string.h>

#define POISON_PACKAGE_AUTHORITIES_PATH "/int/.poison/package_authorities.bin"
#define POISON_UPDATE_AUTHORITIES_PATH  "/int/.poison/content_update_authorities.bin"
#define POISON_PACKAGE_STATE_PATH       "/int/.poison/package_state.bin"
#define POISON_PACKAGE_STATE_DIRECTORY  "/int/.poison"
#define POISON_PACKAGE_MANAGED_ROOT     "/ext/apps/.poison-managed"
#define POISON_PACKAGE_ACTIVE_ROOT      "/ext/apps/PoisonedOS"

static PoisonPackageManager poison_package_service_manager;
static PoisonPackageAuthorityStore poison_package_service_authorities;
static PoisonPackageAuthorityStore poison_content_update_service_authorities;
static bool poison_packages_recover_storage_state(void);

typedef struct {
    uint8_t magic[4u];
    uint32_t version;
    uint32_t manager_bytes;
    PoisonPackageManager manager;
    uint8_t digest[32u];
} PoisonPackageStateFile;

typedef struct {
    PoisonPackageStorageLayout layout;
    char active[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char candidate[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char rollback[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char quarantine[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char swap[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
} PoisonPackageRecoveryPaths;

typedef struct {
    char parent[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char partial[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    PoisonPackageStateFile state;
} PoisonPackageStatePaths;

void poison_packages_on_system_start(void) {
    if(!poison_startup_is_runtime_boot()) return;
    poison_package_manager_init(&poison_package_service_manager);
    if(poison_package_manager_load(&poison_package_service_manager, POISON_PACKAGE_STATE_PATH)) {
        (void)poison_packages_recover_storage_state();
    }
    poison_package_authority_store_init(&poison_package_service_authorities);
    poison_package_authority_store_init(&poison_content_update_service_authorities);
    (void)poison_packages_reload_authorities();
    (void)poison_content_update_reload_authorities();
}

bool poison_packages_save_state(void) {
    return poison_package_manager_save(&poison_package_service_manager, POISON_PACKAGE_STATE_PATH);
}

PoisonPackageManager* poison_packages_manager(void) {
    return &poison_package_service_manager;
}

const PoisonPackageAuthorityStore* poison_packages_authorities(void) {
    return &poison_package_service_authorities;
}

const PoisonPackageAuthorityStore* poison_content_update_authorities(void) {
    return &poison_content_update_service_authorities;
}

static bool poison_package_authority_store_load(
    const char* path,
    PoisonPackageAuthorityStore* destination) {
    if(!path || !destination) return false;
    typedef struct {
        uint8_t encoded[POISON_PACKAGE_AUTHORITY_FILE_MAX];
        PoisonPackageAuthorityStore decoded;
    } AuthorityLoadWorkspace;
    AuthorityLoadWorkspace* workspace = malloc(sizeof(*workspace));
    if(!workspace) {
        poison_package_authority_store_init(destination);
        return false;
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    FileInfo info;
    bool loaded = storage_common_stat(storage, path, &info) == FSE_OK && info.size >= 8u &&
                  info.size <= POISON_PACKAGE_AUTHORITY_FILE_MAX &&
                  storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING);
    size_t bytes_read = 0u;
    if(loaded) bytes_read = storage_file_read(file, workspace->encoded, info.size);
    loaded = loaded && bytes_read == info.size && !storage_file_get_error(file);
    poison_package_authority_store_init(&workspace->decoded);
    loaded = loaded && poison_package_authority_store_decode(
                           &workspace->decoded, workspace->encoded, bytes_read);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    if(!loaded) {
        poison_package_authority_store_init(destination);
    } else {
        *destination = workspace->decoded;
    }
    free(workspace);
    return loaded;
}

bool poison_packages_reload_authorities(void) {
    return poison_package_authority_store_load(
        POISON_PACKAGE_AUTHORITIES_PATH, &poison_package_service_authorities);
}

bool poison_content_update_reload_authorities(void) {
    return poison_package_authority_store_load(
        POISON_UPDATE_AUTHORITIES_PATH, &poison_content_update_service_authorities);
}

static bool poison_package_text_valid(const char* value, size_t maximum) {
    return value && value[0] != '\0' && strnlen(value, maximum + 1u) <= maximum;
}

static bool poison_package_id_valid(const char* value) {
    if(!poison_package_text_valid(value, POISON_PACKAGE_ID_MAX)) return false;
    for(const char* cursor = value; *cursor; ++cursor) {
        if(!(isalnum((unsigned char)*cursor) || *cursor == '.' || *cursor == '-' ||
             *cursor == '_')) {
            return false;
        }
    }
    return true;
}

static bool poison_package_storage_root_valid(const char* path) {
    return path && path[0] == '/' && path[1] != '\0' &&
           strnlen(path, POISON_PACKAGE_MANIFEST_PATH_MAX + 1u) <=
               POISON_PACKAGE_MANIFEST_PATH_MAX &&
           !strstr(path, "..") && !strchr(path, '\\') && path[strlen(path) - 1u] != '/';
}

bool poison_package_storage_layout_init(
    PoisonPackageStorageLayout* layout,
    const char* managed_root,
    const char* active_root) {
    if(!layout || !poison_package_storage_root_valid(managed_root) ||
       !poison_package_storage_root_valid(active_root) || strcmp(managed_root, active_root) == 0) {
        return false;
    }
    memset(layout, 0, sizeof(*layout));
    strcpy(layout->managed_root, managed_root);
    strcpy(layout->active_root, active_root);
    return true;
}

void poison_package_storage_layout_default(PoisonPackageStorageLayout* layout) {
    furi_check(poison_package_storage_layout_init(
        layout, POISON_PACKAGE_MANAGED_ROOT, POISON_PACKAGE_ACTIVE_ROOT));
}

static bool poison_package_storage_path(
    char output[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u],
    const char* root,
    const char* package_id,
    const char* leaf) {
    if(!output || !poison_package_storage_root_valid(root) ||
       !poison_package_id_valid(package_id)) {
        return false;
    }
    const int written =
        leaf ?
            snprintf(
                output, POISON_PACKAGE_MANIFEST_PATH_MAX + 1u, "%s/%s/%s", root, package_id, leaf) :
            snprintf(output, POISON_PACKAGE_MANIFEST_PATH_MAX + 1u, "%s/%s", root, package_id);
    return written > 0 && (size_t)written <= POISON_PACKAGE_MANIFEST_PATH_MAX;
}

static bool poison_package_storage_exists(Storage* storage, const char* path, bool directory) {
    FileInfo info;
    return storage_common_stat(storage, path, &info) == FSE_OK &&
           (((info.flags & FSF_DIRECTORY) != 0u) == directory);
}

static bool poison_package_storage_mkdir_tree(Storage* storage, const char* path) {
    if(!storage || !poison_package_storage_root_valid(path)) return false;
    char current[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    strcpy(current, path);
    for(char* cursor = current + 1u; *cursor; ++cursor) {
        if(*cursor != '/') continue;
        *cursor = '\0';
        if(!poison_package_storage_exists(storage, current, true) &&
           storage_common_mkdir(storage, current) != FSE_OK) {
            return false;
        }
        *cursor = '/';
    }
    return poison_package_storage_exists(storage, current, true) ||
           storage_common_mkdir(storage, current) == FSE_OK;
}

static bool poison_package_storage_remove_if_present(Storage* storage, const char* path) {
    FileInfo info;
    const FS_Error error = storage_common_stat(storage, path, &info);
    if(error == FSE_NOT_EXIST) return true;
    if(error != FSE_OK) return false;
    return (info.flags & FSF_DIRECTORY) != 0u ? storage_simply_remove_recursive(storage, path) :
                                                storage_simply_remove(storage, path);
}

bool poison_package_storage_stage(
    const PoisonPackageStorageLayout* layout,
    const char* archive_path,
    const PoisonPackageVerifiedArchive* verified) {
    if(!layout || !archive_path || !verified ||
       !poison_package_storage_root_valid(layout->managed_root) ||
       !poison_package_storage_root_valid(layout->active_root)) {
        return false;
    }
    char candidate[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    if(!poison_package_storage_path(
           candidate, layout->managed_root, verified->package_id, "candidate")) {
        return false;
    }
    return poison_package_extract_verified_archive(archive_path, verified, candidate);
}

bool poison_package_storage_activate(
    const PoisonPackageStorageLayout* layout,
    const char* package_id) {
    if(!layout) return false;
    char candidate[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char rollback[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char active[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    if(!poison_package_storage_path(candidate, layout->managed_root, package_id, "candidate") ||
       !poison_package_storage_path(rollback, layout->managed_root, package_id, "rollback") ||
       !poison_package_storage_path(active, layout->active_root, package_id, NULL)) {
        return false;
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = poison_package_storage_exists(storage, candidate, true) &&
              poison_package_storage_mkdir_tree(storage, layout->active_root);
    const bool had_active = ok && poison_package_storage_exists(storage, active, true);
    if(ok) ok = poison_package_storage_remove_if_present(storage, rollback);
    if(ok && had_active) ok = storage_common_rename(storage, active, rollback) == FSE_OK;
    if(ok) ok = storage_common_rename(storage, candidate, active) == FSE_OK;
    if(!ok && had_active && !poison_package_storage_exists(storage, active, true) &&
       poison_package_storage_exists(storage, rollback, true)) {
        (void)storage_common_rename(storage, rollback, active);
    }
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool poison_package_storage_report_health(
    const PoisonPackageStorageLayout* layout,
    const char* package_id,
    bool healthy) {
    if(!layout) return false;
    char active[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char rollback[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char quarantine[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    if(!poison_package_storage_path(active, layout->active_root, package_id, NULL) ||
       !poison_package_storage_path(rollback, layout->managed_root, package_id, "rollback") ||
       !poison_package_storage_path(quarantine, layout->managed_root, package_id, "quarantine")) {
        return false;
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = poison_package_storage_exists(storage, active, true);
    if(healthy) {
        furi_record_close(RECORD_STORAGE);
        return ok;
    }
    if(ok) ok = poison_package_storage_remove_if_present(storage, quarantine);
    if(ok) ok = storage_common_rename(storage, active, quarantine) == FSE_OK;
    const bool has_rollback = ok && poison_package_storage_exists(storage, rollback, true);
    if(ok && has_rollback) ok = storage_common_rename(storage, rollback, active) == FSE_OK;
    if(!ok && !poison_package_storage_exists(storage, active, true) &&
       poison_package_storage_exists(storage, quarantine, true)) {
        (void)storage_common_rename(storage, quarantine, active);
    }
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool poison_package_storage_revert_health_rollback(
    const PoisonPackageStorageLayout* layout,
    const char* package_id) {
    if(!layout) return false;
    char active[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char rollback[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char quarantine[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    if(!poison_package_storage_path(active, layout->active_root, package_id, NULL) ||
       !poison_package_storage_path(rollback, layout->managed_root, package_id, "rollback") ||
       !poison_package_storage_path(quarantine, layout->managed_root, package_id, "quarantine")) {
        return false;
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    const bool had_active = poison_package_storage_exists(storage, active, true);
    bool ok = poison_package_storage_exists(storage, quarantine, true) &&
              !poison_package_storage_exists(storage, rollback, true);
    if(ok && had_active) ok = storage_common_rename(storage, active, rollback) == FSE_OK;
    if(ok) ok = storage_common_rename(storage, quarantine, active) == FSE_OK;
    if(!ok && had_active && !poison_package_storage_exists(storage, active, true) &&
       poison_package_storage_exists(storage, rollback, true)) {
        (void)storage_common_rename(storage, rollback, active);
    }
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool poison_package_storage_rollback(
    const PoisonPackageStorageLayout* layout,
    const char* package_id) {
    if(!layout) return false;
    char active[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char rollback[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char swap[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    if(!poison_package_storage_path(active, layout->active_root, package_id, NULL) ||
       !poison_package_storage_path(rollback, layout->managed_root, package_id, "rollback") ||
       !poison_package_storage_path(swap, layout->managed_root, package_id, "swap")) {
        return false;
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = poison_package_storage_exists(storage, active, true) &&
              poison_package_storage_exists(storage, rollback, true) &&
              poison_package_storage_remove_if_present(storage, swap);
    if(ok) ok = storage_common_rename(storage, active, swap) == FSE_OK;
    if(ok) ok = storage_common_rename(storage, rollback, active) == FSE_OK;
    if(ok) ok = storage_common_rename(storage, swap, rollback) == FSE_OK;
    if(!ok) {
        if(poison_package_storage_exists(storage, active, true) &&
           poison_package_storage_exists(storage, swap, true) &&
           !poison_package_storage_exists(storage, rollback, true)) {
            (void)storage_common_rename(storage, active, rollback);
        }
        if(poison_package_storage_exists(storage, swap, true) &&
           !poison_package_storage_exists(storage, active, true)) {
            (void)storage_common_rename(storage, swap, active);
        }
    }
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool poison_package_storage_remove(
    const PoisonPackageStorageLayout* layout,
    const char* package_id) {
    if(!layout) return false;
    char active[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char rollback[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    if(!poison_package_storage_path(active, layout->active_root, package_id, NULL) ||
       !poison_package_storage_path(rollback, layout->managed_root, package_id, "rollback")) {
        return false;
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = poison_package_storage_exists(storage, active, true) &&
              poison_package_storage_remove_if_present(storage, rollback);
    if(ok) ok = storage_common_rename(storage, active, rollback) == FSE_OK;
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool poison_package_storage_restore_removed(
    const PoisonPackageStorageLayout* layout,
    const char* package_id) {
    if(!layout) return false;
    char active[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char rollback[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    if(!poison_package_storage_path(active, layout->active_root, package_id, NULL) ||
       !poison_package_storage_path(rollback, layout->managed_root, package_id, "rollback")) {
        return false;
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    const bool ok = !poison_package_storage_exists(storage, active, true) &&
                    poison_package_storage_exists(storage, rollback, true) &&
                    storage_common_rename(storage, rollback, active) == FSE_OK;
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool poison_package_storage_quarantine(
    const PoisonPackageStorageLayout* layout,
    const char* package_id) {
    if(!layout) return false;
    char candidate[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char active[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char quarantine[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    if(!poison_package_storage_path(candidate, layout->managed_root, package_id, "candidate") ||
       !poison_package_storage_path(active, layout->active_root, package_id, NULL) ||
       !poison_package_storage_path(quarantine, layout->managed_root, package_id, "quarantine")) {
        return false;
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    const char* source = poison_package_storage_exists(storage, candidate, true) ? candidate :
                                                                                   active;
    bool ok = poison_package_storage_exists(storage, source, true) &&
              poison_package_storage_remove_if_present(storage, quarantine);
    if(ok) ok = storage_common_rename(storage, source, quarantine) == FSE_OK;
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool poison_package_storage_restore_quarantine(
    const PoisonPackageStorageLayout* layout,
    const char* package_id,
    bool to_candidate) {
    if(!layout) return false;
    char destination[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char quarantine[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    if(!poison_package_storage_path(
           destination,
           to_candidate ? layout->managed_root : layout->active_root,
           package_id,
           to_candidate ? "candidate" : NULL) ||
       !poison_package_storage_path(quarantine, layout->managed_root, package_id, "quarantine")) {
        return false;
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = poison_package_storage_exists(storage, quarantine, true) &&
              !poison_package_storage_exists(storage, destination, true);
    if(!to_candidate && ok) {
        ok = poison_package_storage_mkdir_tree(storage, layout->active_root);
    }
    if(ok) ok = storage_common_rename(storage, quarantine, destination) == FSE_OK;
    furi_record_close(RECORD_STORAGE);
    return ok;
}

static bool poison_package_digest_valid(const char* value, bool optional) {
    if(optional && (!value || value[0] == '\0')) return true;
    if(!value || strlen(value) != POISON_CONTENT_UPDATE_MAX_DIGEST - 1u) return false;
    for(size_t index = 0; index < POISON_CONTENT_UPDATE_MAX_DIGEST - 1u; ++index) {
        if(!((value[index] >= '0' && value[index] <= '9') ||
             (value[index] >= 'a' && value[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

static bool poison_package_manifest_path_valid(const char* value) {
    static const char prefix[] = "/ext/apps/.staging/";
    return poison_package_text_valid(value, POISON_PACKAGE_MANIFEST_PATH_MAX) &&
           strncmp(value, prefix, sizeof(prefix) - 1u) == 0 && !strstr(value, "..") &&
           !strchr(value, '\\');
}

static PoisonPackageRecord*
    poison_package_manager_find_mutable(PoisonPackageManager* manager, const char* package_id) {
    if(!manager || !package_id) return NULL;
    for(size_t index = 0; index < POISON_PACKAGE_MAX_RECORDS; ++index) {
        PoisonPackageRecord* record = &manager->records[index];
        if(record->occupied && strcmp(record->package_id, package_id) == 0) return record;
    }
    return NULL;
}

bool poison_package_content_type_parse(
    const char* content_type,
    PoisonContentUpdateType* update_type) {
    static const struct {
        const char* name;
        PoisonContentUpdateType type;
    } supported[] = {
        {"application", PoisonContentUpdateApplication},
        {"lesson", PoisonContentUpdateLesson},
        {"tool-data", PoisonContentUpdateToolData},
        {"theme", PoisonContentUpdateTheme},
        {"font", PoisonContentUpdateFontIcon},
        {"icon", PoisonContentUpdateFontIcon},
        {"font-icon", PoisonContentUpdateFontIcon},
        {"menu", PoisonContentUpdateMenu},
        {"resource", PoisonContentUpdateResource},
        {"ui-pack", PoisonContentUpdateUiPack},
    };
    if(!content_type || !update_type) return false;
    for(size_t index = 0u; index < COUNT_OF(supported); index++) {
        if(strcmp(content_type, supported[index].name) == 0) {
            *update_type = supported[index].type;
            return true;
        }
    }
    return false;
}

void poison_package_manager_init(PoisonPackageManager* manager) {
    if(manager) memset(manager, 0, sizeof(*manager));
}

bool poison_package_manager_import(
    PoisonPackageManager* manager,
    const PoisonPackageImport* package) {
    static const char absent_digest[] =
        "0000000000000000000000000000000000000000000000000000000000000000";
    PoisonContentUpdateType update_type;
    if(!manager || !package || !package->manifest_verified ||
       !poison_package_content_type_parse(package->content_type, &update_type) ||
       !poison_package_id_valid(package->package_id) ||
       !poison_package_text_valid(package->version, POISON_PACKAGE_VERSION_MAX) ||
       !poison_package_text_valid(package->signing_key_id, POISON_PACKAGE_SIGNING_KEY_MAX) ||
       !poison_package_digest_valid(package->candidate_digest, false) ||
       !poison_package_digest_valid(package->previous_digest, true) ||
       !poison_package_manifest_path_valid(package->manifest_path) ||
       !poison_package_text_valid(package->entrypoint, POISON_PACKAGE_MANIFEST_PATH_MAX) ||
       package->entrypoint[0] == '/' || strstr(package->entrypoint, "..") ||
       strchr(package->entrypoint, '\\') || package->content_bytes == 0u ||
       package->hardware_target == 0u || package->firmware_api == 0u ||
       package->available_storage_bytes == 0u ||
       (package->previous_state != PoisonPackageRemoved &&
        !poison_package_text_valid(package->previous_version, POISON_PACKAGE_VERSION_MAX))) {
        return false;
    }

    PoisonPackageRecord* record =
        poison_package_manager_find_mutable(manager, package->package_id);
    const bool updating = record && record->transaction.state != PoisonPackageRemoved;
    if(updating) {
        if(strcmp(record->content_type, package->content_type) != 0 ||
           record->transaction.state != package->previous_state ||
           strcmp(record->version, package->previous_version) != 0 ||
           strcmp(record->digest, package->previous_digest) != 0 ||
           strcmp(record->digest, package->candidate_digest) == 0) {
            return false;
        }
    } else {
        if(package->previous_state != PoisonPackageRemoved ||
           (package->previous_version && package->previous_version[0] != '\0') ||
           (package->previous_digest && package->previous_digest[0] != '\0')) {
            return false;
        }
        if(!record) {
            for(size_t index = 0; index < POISON_PACKAGE_MAX_RECORDS; ++index) {
                if(!manager->records[index].occupied) {
                    record = &manager->records[index];
                    break;
                }
            }
        }
        if(!record) {
            for(size_t index = 0; index < POISON_PACKAGE_MAX_RECORDS; ++index) {
                if(manager->records[index].transaction.state == PoisonPackageRemoved) {
                    record = &manager->records[index];
                    break;
                }
            }
        }
    }
    if(!record) return false;

    const char* previous_digest = updating ? record->digest : absent_digest;
    PoisonContentUpdateManifest manifest = {
        .content_type = update_type,
        .update_id = package->package_id,
        .candidate_digest = package->candidate_digest,
        .previous_digest = previous_digest,
        .hardware_target = package->hardware_target,
        .minimum_api = package->firmware_api,
        .maximum_api = UINT32_MAX,
        .release_sequence = package->release_sequence,
        .content_bytes = package->content_bytes,
        .signature_valid = true,
        .rollback_available = true,
        .protected_target = package->protected_package,
    };
    PoisonContentUpdateEnvironment environment = {
        .hardware_target = package->hardware_target,
        .firmware_api = package->firmware_api,
        .highest_release_sequence = updating ? record->transaction.content_update.sequence : 0u,
        .available_storage_bytes = package->available_storage_bytes,
    };
    PoisonPackageTransaction transaction;
    if(poison_package_transaction_begin(
           &transaction,
           &manifest,
           &environment,
           package->previous_state,
           package->protected_package) != PoisonContentUpdateAdmissionOk) {
        return false;
    }

    char previous_version[POISON_PACKAGE_VERSION_MAX + 1u] = {0};
    char retained_digest[POISON_CONTENT_UPDATE_MAX_DIGEST] = {0};
    if(updating) {
        strcpy(previous_version, record->version);
        strcpy(retained_digest, record->digest);
    }
    memset(record, 0, sizeof(*record));
    record->occupied = true;
    strcpy(record->content_type, package->content_type);
    strcpy(record->package_id, package->package_id);
    strcpy(record->version, package->version);
    if(updating) strcpy(record->previous_version, previous_version);
    strcpy(record->digest, package->candidate_digest);
    strcpy(record->previous_digest, updating ? retained_digest : absent_digest);
    strcpy(record->signing_key_id, package->signing_key_id);
    strcpy(record->manifest_path, package->manifest_path);
    strcpy(record->entrypoint, package->entrypoint);
    record->capability_mask = package->capability_mask;
    record->confirmation_required = package->confirmation_required;
    record->transaction = transaction;
    ++manager->generation;
    return true;
}

bool poison_package_manager_receive(
    PoisonPackageManager* manager,
    const char* package_id,
    uint32_t bytes) {
    PoisonPackageRecord* record = poison_package_manager_find_mutable(manager, package_id);
    const bool received = record && poison_package_receive(&record->transaction, bytes);
    if(received) ++manager->generation;
    return received;
}

bool poison_package_manager_verify(
    PoisonPackageManager* manager,
    const char* package_id,
    const char* digest,
    bool payload_verified) {
    PoisonPackageRecord* record = poison_package_manager_find_mutable(manager, package_id);
    if(!record || !payload_verified) {
        if(record) {
            record->transaction.state = PoisonPackageQuarantined;
            record->transaction.content_update.state = PoisonContentUpdateQuarantined;
            ++manager->generation;
        }
        return false;
    }
    const bool verified = poison_package_verify_payload(&record->transaction, digest);
    ++manager->generation;
    return verified;
}

bool poison_package_manager_activate(
    PoisonPackageManager* manager,
    const char* package_id,
    bool exact_confirmation) {
    PoisonPackageRecord* record = poison_package_manager_find_mutable(manager, package_id);
    if(!record || (record->confirmation_required && !exact_confirmation)) return false;
    const bool activated = poison_package_activate(&record->transaction, true);
    if(activated) ++manager->generation;
    return activated;
}

bool poison_package_manager_report_health(
    PoisonPackageManager* manager,
    const char* package_id,
    bool healthy) {
    PoisonPackageRecord* record = poison_package_manager_find_mutable(manager, package_id);
    if(!record || !poison_package_report_health(&record->transaction, healthy)) return false;
    if(!healthy) {
        if(record->previous_version[0]) strcpy(record->version, record->previous_version);
        if(record->previous_digest[0]) strcpy(record->digest, record->previous_digest);
    }
    ++manager->generation;
    return true;
}

bool poison_package_manager_set_enabled(
    PoisonPackageManager* manager,
    const char* package_id,
    bool enabled,
    bool exact_confirmation) {
    PoisonPackageRecord* record = poison_package_manager_find_mutable(manager, package_id);
    if(!record || (enabled && record->confirmation_required && !exact_confirmation)) return false;
    const bool changed = poison_package_transition(
        &record->transaction, enabled ? PoisonPackageActive : PoisonPackageDisabled);
    if(changed) ++manager->generation;
    return changed;
}

bool poison_package_manager_remove(
    PoisonPackageManager* manager,
    const char* package_id,
    bool exact_confirmation) {
    PoisonPackageRecord* record = poison_package_manager_find_mutable(manager, package_id);
    if(!record || !exact_confirmation || record->transaction.protected_package) return false;
    const bool removed = poison_package_transition(&record->transaction, PoisonPackageRemoved);
    if(removed) ++manager->generation;
    return removed;
}

bool poison_package_manager_rollback(
    PoisonPackageManager* manager,
    const char* package_id,
    bool exact_confirmation) {
    PoisonPackageRecord* record = poison_package_manager_find_mutable(manager, package_id);
    if(!record || !exact_confirmation || record->previous_version[0] == '\0' ||
       !poison_package_digest_valid(record->previous_digest, false) ||
       (record->transaction.state != PoisonPackageActive &&
        record->transaction.state != PoisonPackageDisabled) ||
       !poison_content_update_rollback(&record->transaction.content_update)) {
        return false;
    }
    char current_version[POISON_PACKAGE_VERSION_MAX + 1u];
    char current_digest[POISON_CONTENT_UPDATE_MAX_DIGEST];
    strcpy(current_version, record->version);
    strcpy(current_digest, record->digest);
    strcpy(record->version, record->previous_version);
    strcpy(record->digest, record->previous_digest);
    strcpy(record->previous_version, current_version);
    strcpy(record->previous_digest, current_digest);
    record->transaction.state = PoisonPackageActive;
    ++manager->generation;
    return true;
}

bool poison_package_manager_quarantine(PoisonPackageManager* manager, const char* package_id) {
    PoisonPackageRecord* record = poison_package_manager_find_mutable(manager, package_id);
    if(!record || record->transaction.state == PoisonPackageRemoved) return false;
    record->transaction.state = PoisonPackageQuarantined;
    ++manager->generation;
    return true;
}

const PoisonPackageRecord*
    poison_package_manager_find(const PoisonPackageManager* manager, const char* package_id) {
    return poison_package_manager_find_mutable((PoisonPackageManager*)manager, package_id);
}

bool poison_package_manager_active_content(
    const PoisonPackageManager* manager,
    const char* package_id,
    const char* content_type) {
    const PoisonPackageRecord* record = poison_package_manager_find(manager, package_id);
    return record && content_type && strcmp(record->content_type, content_type) == 0 &&
           record->transaction.state == PoisonPackageActive &&
           (record->transaction.content_update.state == PoisonContentUpdateHealthy ||
            record->transaction.content_update.state == PoisonContentUpdateRolledBack);
}

size_t poison_package_manager_count(const PoisonPackageManager* manager) {
    if(!manager) return 0u;
    size_t count = 0u;
    for(size_t index = 0; index < POISON_PACKAGE_MAX_RECORDS; ++index)
        count += manager->records[index].occupied ? 1u : 0u;
    return count;
}

static bool poison_package_storage_recover_record(
    const PoisonPackageStorageLayout* layout,
    PoisonPackageRecoveryPaths* paths,
    const char* package_id,
    bool candidate_committed,
    bool had_previous,
    bool discard_candidate) {
    if(!paths) return false;
    if(!poison_package_storage_path(paths->active, layout->active_root, package_id, NULL) ||
       !poison_package_storage_path(
           paths->candidate, layout->managed_root, package_id, "candidate") ||
       !poison_package_storage_path(
           paths->rollback, layout->managed_root, package_id, "rollback") ||
       !poison_package_storage_path(
           paths->quarantine, layout->managed_root, package_id, "quarantine") ||
       !poison_package_storage_path(paths->swap, layout->managed_root, package_id, "swap")) {
        return false;
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = true;
    bool has_active = poison_package_storage_exists(storage, paths->active, true);
    bool has_rollback = poison_package_storage_exists(storage, paths->rollback, true);
    bool has_swap = poison_package_storage_exists(storage, paths->swap, true);
    if(has_swap && !has_active) {
        ok = storage_common_rename(storage, paths->swap, paths->active) == FSE_OK;
    } else if(has_swap && has_active && !has_rollback) {
        ok = storage_common_rename(storage, paths->active, paths->rollback) == FSE_OK &&
             storage_common_rename(storage, paths->swap, paths->active) == FSE_OK;
    } else if(has_swap) {
        ok = false;
    }
    has_active = poison_package_storage_exists(storage, paths->active, true);
    has_rollback = poison_package_storage_exists(storage, paths->rollback, true);
    const bool has_candidate = poison_package_storage_exists(storage, paths->candidate, true);
    if(ok && !candidate_committed) {
        if(!has_active && has_rollback) {
            ok = storage_common_rename(storage, paths->rollback, paths->active) == FSE_OK;
            has_active = ok;
            has_rollback = false;
        }
        if(ok && !has_candidate && has_active && (!had_previous || has_rollback)) {
            ok = poison_package_storage_remove_if_present(storage, paths->quarantine) &&
                 storage_common_rename(storage, paths->active, paths->quarantine) == FSE_OK;
            if(ok && has_rollback) {
                ok = storage_common_rename(storage, paths->rollback, paths->active) == FSE_OK;
                has_active = ok;
                has_rollback = false;
            } else {
                has_active = false;
            }
        } else if(
            ok && has_candidate && discard_candidate &&
            !poison_package_storage_exists(storage, paths->quarantine, true)) {
            ok = storage_common_rename(storage, paths->candidate, paths->quarantine) == FSE_OK;
        }
    }
    if(ok && candidate_committed && !has_active && has_rollback) {
        ok = storage_common_rename(storage, paths->rollback, paths->active) == FSE_OK;
        has_active = ok;
    }
    if(ok && candidate_committed && !has_active) ok = false;
    furi_record_close(RECORD_STORAGE);
    return ok;
}

static bool poison_packages_recover_storage_state(void) {
    PoisonPackageRecoveryPaths* workspace = malloc(sizeof(*workspace));
    if(!workspace) return false;
    poison_package_storage_layout_default(&workspace->layout);
    bool changed = false;
    for(size_t index = 0; index < POISON_PACKAGE_MAX_RECORDS; ++index) {
        PoisonPackageRecord* record = &poison_package_service_manager.records[index];
        if(!record->occupied) continue;
        if(record->transaction.state == PoisonPackageRemoved) continue;
        const bool activating = record->transaction.content_update.state ==
                                PoisonContentUpdateActivating;
        const bool committed = record->transaction.state == PoisonPackageActive ||
                               record->transaction.state == PoisonPackageDisabled ||
                               record->transaction.state == PoisonPackageInstalled;
        const bool had_previous = record->transaction.rollback_state != PoisonPackageRemoved;
        if(!poison_package_storage_recover_record(
               &workspace->layout,
               workspace,
               record->package_id,
               committed && !activating,
               had_previous,
               activating)) {
            record->transaction.state = PoisonPackageQuarantined;
            record->transaction.content_update.state = PoisonContentUpdateQuarantined;
            ++poison_package_service_manager.generation;
            changed = true;
            continue;
        }
        if(activating) {
            if(poison_package_manager_report_health(
                   &poison_package_service_manager, record->package_id, false)) {
                changed = true;
            } else {
                record->transaction.state = PoisonPackageQuarantined;
                record->transaction.content_update.state = PoisonContentUpdateQuarantined;
                ++poison_package_service_manager.generation;
                changed = true;
            }
        }
    }
    const bool recovered = !changed || poison_packages_save_state();
    free(workspace);
    return recovered;
}

static bool poison_package_state_digest(const PoisonPackageStateFile* state, uint8_t digest[32u]) {
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool ok = mbedtls_sha256_starts(&hash, 0) == 0;
    ok = ok && mbedtls_sha256_update(
                   &hash, (const uint8_t*)state, offsetof(PoisonPackageStateFile, digest)) == 0;
    ok = ok && mbedtls_sha256_finish(&hash, digest) == 0;
    mbedtls_sha256_free(&hash);
    return ok;
}

static bool poison_package_record_persisted_valid(const PoisonPackageRecord* record) {
    PoisonContentUpdateType update_type;
    if(!record || !record->occupied ||
       !poison_package_content_type_parse(record->content_type, &update_type) ||
       !poison_package_id_valid(record->package_id) ||
       !poison_package_text_valid(record->version, POISON_PACKAGE_VERSION_MAX) ||
       !poison_package_digest_valid(record->digest, false) ||
       !poison_package_digest_valid(record->previous_digest, false) ||
       !poison_package_text_valid(record->signing_key_id, POISON_PACKAGE_SIGNING_KEY_MAX) ||
       !poison_package_manifest_path_valid(record->manifest_path) ||
       !poison_package_text_valid(record->entrypoint, POISON_PACKAGE_MANIFEST_PATH_MAX) ||
       record->entrypoint[0] == '/' || strstr(record->entrypoint, "..") ||
       strchr(record->entrypoint, '\\') || !record->transaction.initialized ||
       record->transaction.state > PoisonPackageRemoved ||
       record->transaction.rollback_state > PoisonPackageRemoved ||
       record->transaction.content_update.state > PoisonContentUpdateQuarantined ||
       strcmp(record->transaction.content_update.update_id, record->package_id) != 0 ||
       ((record->transaction.content_update.state == PoisonContentUpdateRolledBack &&
         strcmp(record->transaction.content_update.previous_digest, record->digest) != 0) ||
        (record->transaction.content_update.state != PoisonContentUpdateRolledBack &&
         strcmp(record->transaction.content_update.candidate_digest, record->digest) != 0)) ||
       record->transaction.content_update.content_type != update_type ||
       record->transaction.content_update.sequence == 0u ||
       record->transaction.content_update.content_bytes == 0u ||
       record->transaction.content_update.received_bytes >
           record->transaction.content_update.content_bytes) {
        return false;
    }
    return record->previous_version[0] == '\0' ||
           poison_package_text_valid(record->previous_version, POISON_PACKAGE_VERSION_MAX);
}

static bool poison_package_manager_persisted_valid(const PoisonPackageManager* manager) {
    if(!manager) return false;
    size_t occupied = 0u;
    for(size_t index = 0; index < POISON_PACKAGE_MAX_RECORDS; ++index) {
        const PoisonPackageRecord* record = &manager->records[index];
        if(!record->occupied) continue;
        if(!poison_package_record_persisted_valid(record)) return false;
        for(size_t prior = 0; prior < index; ++prior) {
            if(manager->records[prior].occupied &&
               strcmp(manager->records[prior].package_id, record->package_id) == 0) {
                return false;
            }
        }
        ++occupied;
    }
    return occupied <= POISON_PACKAGE_MAX_RECORDS;
}

static bool poison_package_state_parent(
    const char* state_path,
    char parent[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u]) {
    if(!poison_package_storage_root_valid(state_path)) return false;
    strcpy(parent, state_path);
    char* separator = strrchr(parent, '/');
    if(!separator || separator == parent) return false;
    *separator = '\0';
    return true;
}

bool poison_package_manager_save(const PoisonPackageManager* manager, const char* state_path) {
    if(!poison_package_manager_persisted_valid(manager)) return false;
    PoisonPackageStatePaths* workspace = malloc(sizeof(*workspace));
    if(!workspace) return false;
    if(!poison_package_state_parent(state_path, workspace->parent) ||
       snprintf(workspace->partial, sizeof(workspace->partial), "%s.partial", state_path) >=
           (int)sizeof(workspace->partial)) {
        free(workspace);
        return false;
    }
    PoisonPackageStateFile* state = &workspace->state;
    memset(state, 0, sizeof(*state));
    memcpy(state->magic, "PPKS", sizeof(state->magic));
    state->version = 1u;
    state->manager_bytes = sizeof(state->manager);
    state->manager = *manager;
    bool ok = poison_package_state_digest(state, state->digest);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(ok) ok = poison_package_storage_mkdir_tree(storage, workspace->parent);
    File* file = storage_file_alloc(storage);
    if(ok) ok = storage_file_open(file, workspace->partial, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) ok = storage_file_write(file, state, sizeof(*state)) == sizeof(*state);
    if(ok) ok = storage_file_sync(file);
    if(storage_file_is_open(file)) ok = storage_file_close(file) && ok;
    if(ok) ok = storage_common_rename(storage, workspace->partial, state_path) == FSE_OK;
    storage_file_free(file);
    if(!ok) (void)storage_simply_remove(storage, workspace->partial);
    furi_record_close(RECORD_STORAGE);
    free(workspace);
    return ok;
}

bool poison_package_manager_load(PoisonPackageManager* manager, const char* state_path) {
    if(!manager || !poison_package_storage_root_valid(state_path)) return false;
    PoisonPackageStateFile* state = malloc(sizeof(*state));
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
    ok = ok && memcmp(state->magic, "PPKS", sizeof(state->magic)) == 0 && state->version == 1u &&
         state->manager_bytes == sizeof(state->manager) &&
         poison_package_state_digest(state, digest) &&
         memcmp(digest, state->digest, sizeof(digest)) == 0 &&
         poison_package_manager_persisted_valid(&state->manager);
    if(ok) *manager = state->manager;
    memset(digest, 0, sizeof(digest));
    memset(state, 0, sizeof(*state));
    free(state);
    return ok;
}
