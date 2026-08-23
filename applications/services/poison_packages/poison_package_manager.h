#pragma once

#include "poison_package_authority.h"
#include "poison_package_archive.h"
#include "poison_package_transaction.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_PACKAGE_MAX_RECORDS       (8u)
#define POISON_PACKAGE_ID_MAX            (64u)
#define POISON_PACKAGE_VERSION_MAX       (32u)
#define POISON_PACKAGE_CONTENT_TYPE_MAX  (32u)
#define POISON_PACKAGE_SIGNING_KEY_MAX   (64u)
#define POISON_PACKAGE_MANIFEST_PATH_MAX (256u)

typedef struct {
    const char* content_type;
    const char* package_id;
    const char* version;
    const char* previous_version;
    const char* candidate_digest;
    const char* previous_digest;
    const char* signing_key_id;
    const char* manifest_path;
    const char* entrypoint;
    uint64_t capability_mask;
    uint32_t release_sequence;
    uint32_t content_bytes;
    uint32_t hardware_target;
    uint32_t firmware_api;
    uint32_t highest_release_sequence;
    uint64_t available_storage_bytes;
    PoisonPackageState previous_state;
    bool protected_package;
    bool confirmation_required;
    bool manifest_verified;
} PoisonPackageImport;

typedef struct {
    bool occupied;
    char content_type[POISON_PACKAGE_CONTENT_TYPE_MAX + 1u];
    char package_id[POISON_PACKAGE_ID_MAX + 1u];
    char version[POISON_PACKAGE_VERSION_MAX + 1u];
    char previous_version[POISON_PACKAGE_VERSION_MAX + 1u];
    char digest[POISON_CONTENT_UPDATE_MAX_DIGEST];
    char previous_digest[POISON_CONTENT_UPDATE_MAX_DIGEST];
    char signing_key_id[POISON_PACKAGE_SIGNING_KEY_MAX + 1u];
    char manifest_path[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char entrypoint[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    uint64_t capability_mask;
    bool confirmation_required;
    PoisonPackageTransaction transaction;
} PoisonPackageRecord;

typedef struct {
    PoisonPackageRecord records[POISON_PACKAGE_MAX_RECORDS];
    uint32_t generation;
} PoisonPackageManager;

typedef struct {
    char managed_root[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
    char active_root[POISON_PACKAGE_MANIFEST_PATH_MAX + 1u];
} PoisonPackageStorageLayout;

void poison_package_manager_init(PoisonPackageManager* manager);
bool poison_package_manager_import(
    PoisonPackageManager* manager,
    const PoisonPackageImport* package);
bool poison_package_manager_receive(
    PoisonPackageManager* manager,
    const char* package_id,
    uint32_t bytes);
bool poison_package_manager_verify(
    PoisonPackageManager* manager,
    const char* package_id,
    const char* digest,
    bool payload_verified);
bool poison_package_manager_activate(
    PoisonPackageManager* manager,
    const char* package_id,
    bool exact_confirmation);
bool poison_package_manager_report_health(
    PoisonPackageManager* manager,
    const char* package_id,
    bool healthy);
bool poison_package_manager_set_enabled(
    PoisonPackageManager* manager,
    const char* package_id,
    bool enabled,
    bool exact_confirmation);
bool poison_package_manager_remove(
    PoisonPackageManager* manager,
    const char* package_id,
    bool exact_confirmation);
bool poison_package_manager_rollback(
    PoisonPackageManager* manager,
    const char* package_id,
    bool exact_confirmation);
bool poison_package_manager_quarantine(PoisonPackageManager* manager, const char* package_id);
const PoisonPackageRecord*
    poison_package_manager_find(const PoisonPackageManager* manager, const char* package_id);
bool poison_package_content_type_parse(
    const char* content_type,
    PoisonContentUpdateType* update_type);
bool poison_package_manager_active_content(
    const PoisonPackageManager* manager,
    const char* package_id,
    const char* content_type);
size_t poison_package_manager_count(const PoisonPackageManager* manager);
bool poison_package_manager_save(const PoisonPackageManager* manager, const char* state_path);
bool poison_package_manager_load(PoisonPackageManager* manager, const char* state_path);

bool poison_package_storage_layout_init(
    PoisonPackageStorageLayout* layout,
    const char* managed_root,
    const char* active_root);
void poison_package_storage_layout_default(PoisonPackageStorageLayout* layout);
bool poison_package_storage_stage(
    const PoisonPackageStorageLayout* layout,
    const char* archive_path,
    const PoisonPackageVerifiedArchive* verified);
bool poison_package_storage_activate(
    const PoisonPackageStorageLayout* layout,
    const char* package_id);
bool poison_package_storage_report_health(
    const PoisonPackageStorageLayout* layout,
    const char* package_id,
    bool healthy);
bool poison_package_storage_revert_health_rollback(
    const PoisonPackageStorageLayout* layout,
    const char* package_id);
bool poison_package_storage_rollback(
    const PoisonPackageStorageLayout* layout,
    const char* package_id);
bool poison_package_storage_remove(
    const PoisonPackageStorageLayout* layout,
    const char* package_id);
bool poison_package_storage_restore_removed(
    const PoisonPackageStorageLayout* layout,
    const char* package_id);
bool poison_package_storage_quarantine(
    const PoisonPackageStorageLayout* layout,
    const char* package_id);
bool poison_package_storage_restore_quarantine(
    const PoisonPackageStorageLayout* layout,
    const char* package_id,
    bool to_candidate);

PoisonPackageManager* poison_packages_manager(void);
const PoisonPackageAuthorityStore* poison_packages_authorities(void);
const PoisonPackageAuthorityStore* poison_content_update_authorities(void);
bool poison_packages_reload_authorities(void);
bool poison_content_update_reload_authorities(void);
bool poison_packages_save_state(void);

#ifdef __cplusplus
}
#endif
