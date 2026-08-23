#pragma once
#include "rpc.h"
#include "rpc_poison_content_update.h"
#include "rpc_poison_channel.h"
#include "rpc_poison_session.h"
#include <storage/filesystem_api_defines.h>
#include <pb.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include <flipper.pb.h>
#include <toolbox/pipe.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* (*RpcSystemAlloc)(RpcSession* session);
typedef void (*RpcSystemFree)(void* context);
typedef void (*PBMessageHandler)(const PB_Main* msg_request, void* context);
typedef bool (*RpcPairingConfirmationCallback)(
    void* context,
    const char* confirmation_code,
    const char* fingerprint,
    const char* client_name,
    uint32_t requested_role,
    uint32_t requested_capabilities);
typedef bool (*RpcContentUpdateConfirmationCallback)(
    void* context,
    const char* update_id,
    const char* candidate_digest,
    uint32_t content_type);
typedef bool (*RpcProfileConfirmationCallback)(
    void* context,
    const char* profile_id,
    const char* version,
    uint64_t capability_mask);
typedef bool (*RpcCancellationCallback)(uint32_t command_id, const char* reason, void* context);

typedef struct {
    bool (*decode_submessage)(pb_istream_t* stream, const pb_field_t* field, void** arg);
    PBMessageHandler message_handler;
    void* context;
} RpcHandler;

void rpc_send(RpcSession* session, PB_Main* main_message);

void rpc_send_and_release(RpcSession* session, PB_Main* main_message);

void rpc_send_and_release_empty(RpcSession* session, uint32_t command_id, PB_CommandStatus status);

void rpc_add_handler(RpcSession* session, pb_size_t message_tag, RpcHandler* handler);

void rpc_session_set_pairing_confirmation_callback(
    RpcSession* session,
    RpcPairingConfirmationCallback callback,
    void* context);

bool rpc_session_request_pairing_confirmation(
    RpcSession* session,
    const char* confirmation_code,
    const char* fingerprint,
    const char* client_name,
    uint32_t requested_role,
    uint32_t requested_capabilities);

void rpc_session_set_content_update_confirmation_callback(
    RpcSession* session,
    RpcContentUpdateConfirmationCallback callback,
    void* context);

bool rpc_session_request_content_update_confirmation(
    RpcSession* session,
    const char* update_id,
    const char* candidate_digest,
    uint32_t content_type);
bool rpc_session_request_profile_confirmation(
    RpcSession* session,
    const char* profile_id,
    const char* version,
    uint64_t capability_mask);
void rpc_session_set_profile_confirmation_callback(
    RpcSession* session,
    RpcProfileConfirmationCallback callback,
    void* context);

void rpc_session_activate_secure_transport(RpcSession* session, PoisonSession* poison_session);

bool rpc_session_dispatch_secure_payload(
    RpcSession* session,
    PoisonSession* poison_session,
    const char* channel,
    uint64_t acknowledgement,
    const uint8_t* payload,
    size_t payload_length);

bool rpc_session_is_secure_dispatch_active(RpcSession* session);

bool rpc_session_get_secure_identity(RpcSession* session, uint64_t* session_id, PoisonRole* role);

bool rpc_session_get_secure_authorization(
    RpcSession* session,
    uint64_t* session_id,
    PoisonRole* role,
    PoisonCapability* capabilities);

bool rpc_session_get_secure_actor_identity(RpcSession* session, uint8_t actor_identity_digest[32u]);

bool rpc_session_get_audit_context(
    RpcSession* session,
    uint32_t command_id,
    uint8_t actor_digest[32u],
    uint8_t correlation_id[32u]);

RpcPoisonContentUpdate* rpc_session_content_update(RpcSession* session);

bool rpc_session_store_resume(
    RpcSession* session,
    const PoisonSession* poison_session,
    const uint8_t token[POISON_SESSION_RESUME_TOKEN_BYTES]);

bool rpc_session_take_resume(
    RpcSession* session,
    uint64_t session_id,
    const uint8_t token[POISON_SESSION_RESUME_TOKEN_BYTES],
    uint64_t last_received_sequence,
    PoisonSession* resumed_session);

void rpc_session_revoke_resume(RpcSession* session, uint64_t session_id);

void rpc_session_revoke_secure_transport(RpcSession* session);

bool rpc_session_register_cancellable(
    RpcSession* session,
    uint32_t command_id,
    RpcCancellationCallback callback,
    void* context);

void rpc_session_complete_cancellable(RpcSession* session, uint32_t command_id, void* context);

bool rpc_session_open_secure_channel(
    RpcSession* session,
    const char* channel,
    uint32_t credits,
    uint64_t next_sequence,
    uint32_t* granted_credits,
    uint64_t* granted_sequence);

bool rpc_session_add_secure_channel_credits(
    RpcSession* session,
    const char* channel,
    uint32_t credits);

bool rpc_session_secure_channel_is_open(RpcSession* session, const char* channel);

void* rpc_system_system_alloc(RpcSession* session);
void* rpc_system_storage_alloc(RpcSession* session);
void rpc_system_storage_free(void* ctx);
void* rpc_system_app_alloc(RpcSession* session);
void rpc_system_app_free(void* ctx);
void* rpc_system_gui_alloc(RpcSession* session);
void rpc_system_gui_free(void* ctx);
void* rpc_system_gpio_alloc(RpcSession* session);
void rpc_system_gpio_free(void* ctx);
void* rpc_system_property_alloc(RpcSession* session);
void* rpc_system_poison_alloc(RpcSession* session);
void rpc_system_poison_free(void* context);
void* rpc_system_poison_channel_alloc(RpcSession* session);
void rpc_system_poison_channel_free(void* context);
void* rpc_system_poison_packages_alloc(RpcSession* session);
void rpc_system_poison_packages_free(void* context);
void* rpc_system_poison_package_catalog_alloc(RpcSession* session);
void rpc_system_poison_package_catalog_free(void* context);
void* rpc_system_poison_app_alloc(RpcSession* session);
void rpc_system_poison_app_free(void* context);
void* rpc_system_poison_profiles_alloc(RpcSession* session);
void rpc_system_poison_profiles_free(void* context);
void* rpc_system_poison_tools_alloc(RpcSession* session);
void rpc_system_poison_tools_free(void* context);
void* rpc_system_poison_files_alloc(RpcSession* session);
void rpc_system_poison_files_free(void* context);
void* rpc_system_poison_evidence_alloc(RpcSession* session);
void rpc_system_poison_evidence_free(void* context);
void* rpc_system_poison_audit_alloc(RpcSession* session);
void rpc_system_poison_audit_free(void* context);
void* rpc_system_poison_workload_alloc(RpcSession* session);
void rpc_system_poison_workload_free(void* context);
void* rpc_system_poison_js_bundle_alloc(RpcSession* session);
void rpc_system_poison_js_bundle_free(void* context);

void* rpc_desktop_alloc(RpcSession* session);
void rpc_desktop_free(void* ctx);

void rpc_debug_print_message(const PB_Main* message);
void rpc_debug_print_data(const char* prefix, uint8_t* buffer, size_t size);

void rpc_cli_command_start_session(PipeSide* pipe, FuriString* args, void* context);

PB_CommandStatus rpc_system_storage_get_error(FS_Error fs_error);

#ifdef __cplusplus
}
#endif
