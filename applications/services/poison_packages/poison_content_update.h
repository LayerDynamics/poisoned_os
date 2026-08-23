#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_CONTENT_UPDATE_MAX_ID     64u
#define POISON_CONTENT_UPDATE_MAX_DIGEST 65u

typedef enum {
    PoisonContentUpdateDiscovered,
    PoisonContentUpdateReceiving,
    PoisonContentUpdateStaged,
    PoisonContentUpdateVerified,
    PoisonContentUpdateAwaitingConfirmation,
    PoisonContentUpdateActivating,
    PoisonContentUpdateHealthy,
    PoisonContentUpdateRolledBack,
    PoisonContentUpdateQuarantined,
} PoisonContentUpdateState;

typedef enum {
    PoisonContentUpdateApplication,
    PoisonContentUpdateFirmware,
    PoisonContentUpdateLesson,
    PoisonContentUpdateToolData,
    PoisonContentUpdateTheme,
    PoisonContentUpdateFontIcon,
    PoisonContentUpdateMenu,
    PoisonContentUpdateResource,
    PoisonContentUpdateUiPack,
    PoisonContentUpdateTypeCount,
} PoisonContentUpdateType;

typedef enum {
    PoisonContentUpdateAdmissionOk,
    PoisonContentUpdateAdmissionInvalid,
    PoisonContentUpdateAdmissionTampered,
    PoisonContentUpdateAdmissionWrongTarget,
    PoisonContentUpdateAdmissionIncompatibleApi,
    PoisonContentUpdateAdmissionDowngrade,
    PoisonContentUpdateAdmissionRevokedSigner,
    PoisonContentUpdateAdmissionInsufficientStorage,
    PoisonContentUpdateAdmissionMissingRollback,
} PoisonContentUpdateAdmission;

typedef struct {
    PoisonContentUpdateType content_type;
    const char* update_id;
    const char* candidate_digest;
    const char* previous_digest;
    uint32_t hardware_target;
    uint32_t minimum_api;
    uint32_t maximum_api;
    uint32_t release_sequence;
    uint32_t content_bytes;
    bool signature_valid;
    bool signer_revoked;
    bool rollback_available;
    bool protected_target;
} PoisonContentUpdateManifest;

typedef struct {
    uint32_t hardware_target;
    uint32_t firmware_api;
    uint32_t highest_release_sequence;
    uint32_t available_storage_bytes;
} PoisonContentUpdateEnvironment;

typedef struct {
    char update_id[POISON_CONTENT_UPDATE_MAX_ID];
    char candidate_digest[POISON_CONTENT_UPDATE_MAX_DIGEST];
    char previous_digest[POISON_CONTENT_UPDATE_MAX_DIGEST];
    PoisonContentUpdateState state;
    PoisonContentUpdateType content_type;
    uint32_t sequence;
    uint32_t content_bytes;
    uint32_t received_bytes;
    bool rollback_available;
    bool confirmation_required;
    bool confirmation_authorized;
    bool payload_verified;
    bool health_reported;
} PoisonContentUpdate;

PoisonContentUpdateAdmission poison_content_update_admit(
    PoisonContentUpdate* update,
    const PoisonContentUpdateManifest* manifest,
    const PoisonContentUpdateEnvironment* environment);

bool poison_content_update_begin(
    PoisonContentUpdate* update,
    const char* update_id,
    const char* candidate_digest,
    const char* previous_digest,
    uint32_t sequence);
bool poison_content_update_transition(PoisonContentUpdate* update, PoisonContentUpdateState next);
bool poison_content_update_receive(PoisonContentUpdate* update, uint32_t bytes);
bool poison_content_update_verify_payload(PoisonContentUpdate* update, const char* actual_digest);
bool poison_content_update_confirm(PoisonContentUpdate* update, bool exact_confirmation);
bool poison_content_update_can_activate(const PoisonContentUpdate* update);
bool poison_content_update_report_health(PoisonContentUpdate* update, bool healthy);
bool poison_content_update_recover(PoisonContentUpdate* update, bool rollback_artifact_valid);
bool poison_content_update_cancel(PoisonContentUpdate* update);
bool poison_content_update_rollback(PoisonContentUpdate* update);

#ifdef __cplusplus
}
#endif
