#pragma once

#include "../poison_packages/poison_content_update.h"
#include "../poison_packages/poison_package_archive.h"
#include "poison_confirmation.h"

#include <poison_packages.pb.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_CONTENT_UPDATE_CONFIRMATION_TOKEN_BYTES (16u)
#define POISON_CONTENT_UPDATE_MANIFEST_PATH_BYTES      (257u)
#define POISON_CONTENT_UPDATE_STATE_PATH               "/int/.poison/content_update_state.bin"
#define POISON_CONTENT_UPDATE_BOOTSTRAP_LKG_PATH       "/ext/update/poison-lkg/update.fuf"

typedef bool (*RpcPoisonContentUpdateActivationCallback)(void* context, const char* manifest_path);
typedef bool (*RpcPoisonContentUpdateRollbackCallback)(void* context, const char* manifest_path);
typedef bool (*RpcPoisonContentUpdateVerificationCallback)(
    void* context,
    const char* manifest_path,
    const char* candidate_digest,
    PoisonPackageVerifiedArchive* verified);

typedef struct {
    uint64_t session_id;
    PoisonRole role;
    uint32_t policy_version;
    uint64_t now_ms;
    bool physical_confirmed;
} RpcPoisonContentUpdateRequestContext;

typedef struct {
    PoisonContentUpdate update;
    PoisonPackageVerifiedArchive verified_archive;
    PoisonConfirmation confirmation;
    char manifest_path[POISON_CONTENT_UPDATE_MANIFEST_PATH_BYTES];
    char activation_manifest_path[POISON_CONTENT_UPDATE_MANIFEST_PATH_BYTES];
    char last_known_good_manifest_path[POISON_CONTENT_UPDATE_MANIFEST_PATH_BYTES];
    char previous_known_good_manifest_path[POISON_CONTENT_UPDATE_MANIFEST_PATH_BYTES];
    char accepted_digest[POISON_CONTENT_UPDATE_MAX_DIGEST];
    RpcPoisonContentUpdateActivationCallback activation_callback;
    void* activation_context;
    RpcPoisonContentUpdateRollbackCallback rollback_callback;
    void* rollback_context;
    RpcPoisonContentUpdateVerificationCallback verification_callback;
    void* verification_context;
    bool active;
    uint32_t hardware_target;
    uint32_t firmware_api;
    uint32_t highest_release_sequence;
    uint64_t available_storage_bytes;
} RpcPoisonContentUpdate;

void rpc_poison_content_update_init(RpcPoisonContentUpdate* engine);

void rpc_poison_content_update_set_environment(
    RpcPoisonContentUpdate* engine,
    uint32_t hardware_target,
    uint32_t firmware_api,
    uint32_t highest_release_sequence,
    uint64_t available_storage_bytes);

void rpc_poison_content_update_set_activation_callback(
    RpcPoisonContentUpdate* engine,
    RpcPoisonContentUpdateActivationCallback callback,
    void* context);
void rpc_poison_content_update_set_rollback_callback(
    RpcPoisonContentUpdate* engine,
    RpcPoisonContentUpdateRollbackCallback callback,
    void* context);

void rpc_poison_content_update_set_verification_callback(
    RpcPoisonContentUpdate* engine,
    RpcPoisonContentUpdateVerificationCallback callback,
    void* context);

bool rpc_poison_content_update_set_last_known_good(
    RpcPoisonContentUpdate* engine,
    const char* manifest_path);
bool rpc_poison_content_update_promote_last_known_good(
    RpcPoisonContentUpdate* engine,
    const char* manifest_path);

bool rpc_poison_content_update_process(
    RpcPoisonContentUpdate* engine,
    const PB_Poison_ContentUpdateRequest* request,
    const RpcPoisonContentUpdateRequestContext* request_context,
    PB_Poison_ContentUpdateStatus* status);

bool rpc_poison_content_update_confirmation_matches(
    const RpcPoisonContentUpdate* engine,
    const PB_Poison_ContentUpdateRequest* request,
    const RpcPoisonContentUpdateRequestContext* request_context);

bool rpc_poison_content_update_save(const RpcPoisonContentUpdate* engine, const char* state_path);
bool rpc_poison_content_update_load(RpcPoisonContentUpdate* engine, const char* state_path);

#ifdef __cplusplus
}
#endif
