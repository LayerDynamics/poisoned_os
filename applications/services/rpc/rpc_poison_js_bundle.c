#include "rpc_i.h"

#include "../poison_audit/poison_audit.h"
#include "../poison_workload/poison_js_bundle_i.h"

#include <furi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POISON_JS_BUNDLE_RPC_CHUNK_BYTES (384u)

typedef struct {
    RpcSession* session;
    PoisonJsBundle* bundle;
} RpcPoisonJsBundle;

bool poison_js_bundle_rpc_validate(const PoisonJsBundleMetadata* metadata) {
    return poison_js_bundle_metadata_valid(metadata);
}

static bool rpc_poison_js_bundle_identity_matches(
    const PoisonJsBundle* bundle,
    const PB_Poison_JsBundleRequest* request) {
    return bundle && poison_js_bundle_rpc_validate(&bundle->metadata) &&
           strcmp(bundle->metadata.id, request->bundle_id) == 0 &&
           strcmp(bundle->metadata.version, request->version) == 0 &&
           strcmp(bundle->metadata.content_digest, request->content_sha256) == 0;
}

static bool rpc_poison_js_bundle_request_valid(const PB_Poison_JsBundleRequest* request) {
    if(!request || request->bundle_id[0] == '\0' || request->version[0] == '\0' ||
       !poison_js_bundle_digest_valid(request->content_sha256)) {
        return false;
    }
    switch(request->operation) {
    case PB_Poison_JsBundleOperation_JS_BUNDLE_OPERATION_DESCRIBE:
        return request->asset_path[0] == '\0' && request->offset == 0u && request->length == 0u;
    case PB_Poison_JsBundleOperation_JS_BUNDLE_OPERATION_READ_ASSET:
        return poison_js_bundle_path_valid(request->asset_path) && request->length > 0u &&
               request->length <= POISON_JS_BUNDLE_MAX_READ_BYTES &&
               request->offset < POISON_JS_BUNDLE_MAX_BYTES;
    default:
        return false;
    }
}

static void rpc_poison_js_bundle_fill_status(
    const RpcPoisonJsBundle* system,
    bool accepted,
    const char* message,
    PB_Poison_JsBundleStatus* status) {
    memset(status, 0, sizeof(*status));
    status->accepted = accepted;
    snprintf(status->message, sizeof(status->message), "%s", message);
    if(!system->bundle || !poison_js_bundle_rpc_validate(&system->bundle->metadata)) return;
    const PoisonJsBundleMetadata* metadata = &system->bundle->metadata;
    strcpy(status->bundle_id, metadata->id);
    strcpy(status->version, metadata->version);
    status->api_version = metadata->api_version;
    strcpy(status->entrypoint, metadata->entrypoint);
    strcpy(status->content_sha256, metadata->content_digest);
    status->size = metadata->size;
    status->capability_count = metadata->capability_count;
    status->asset_count = system->bundle->asset_count;
}

static void rpc_poison_js_bundle_send_status(
    RpcPoisonJsBundle* system,
    uint32_t command_id,
    bool accepted,
    const char* message) {
    PB_Main response = PB_Main_init_zero;
    response.command_id = command_id;
    response.command_status = PB_CommandStatus_OK;
    response.which_content = PB_Main_poison_js_bundle_status_tag;
    rpc_poison_js_bundle_fill_status(
        system, accepted, message, &response.content.poison_js_bundle_status);
    rpc_send_and_release(system->session, &response);
}

static void rpc_poison_js_bundle_send_inventory(RpcPoisonJsBundle* system, uint32_t command_id) {
    for(size_t index = 0u; index < system->bundle->metadata.capability_count; ++index) {
        PB_Main response = PB_Main_init_zero;
        response.command_id = command_id;
        response.command_status = PB_CommandStatus_OK;
        response.has_next = true;
        response.which_content = PB_Main_poison_js_bundle_frame_tag;
        PB_Poison_JsBundleFrame* frame = &response.content.poison_js_bundle_frame;
        frame->kind = PB_Poison_JsBundleFrameKind_JS_BUNDLE_FRAME_CAPABILITY;
        strcpy(frame->capability, system->bundle->capabilities[index]);
        rpc_send_and_release(system->session, &response);
    }
    for(size_t index = 0u; index < system->bundle->asset_count; ++index) {
        const PoisonJsBundleAsset* asset = &system->bundle->assets[index];
        PB_Main response = PB_Main_init_zero;
        response.command_id = command_id;
        response.command_status = PB_CommandStatus_OK;
        response.has_next = true;
        response.which_content = PB_Main_poison_js_bundle_frame_tag;
        PB_Poison_JsBundleFrame* frame = &response.content.poison_js_bundle_frame;
        frame->kind = PB_Poison_JsBundleFrameKind_JS_BUNDLE_FRAME_ASSET;
        strcpy(frame->asset_path, asset->path);
        strcpy(frame->asset_sha256, asset->sha256);
        frame->asset_size = asset->size;
        rpc_send_and_release(system->session, &response);
    }
}

static bool rpc_poison_js_bundle_send_asset(
    RpcPoisonJsBundle* system,
    const PB_Poison_JsBundleRequest* request,
    uint32_t command_id) {
    const PoisonJsBundleAsset* asset =
        poison_js_bundle_find_asset(system->bundle, request->asset_path);
    if(!asset || request->offset >= asset->size) return false;
    uint8_t* data = malloc(request->length);
    if(!data) return false;
    size_t data_size = 0u;
    bool eof = false;
    bool sent = poison_js_bundle_read_asset(
        system->bundle,
        request->asset_path,
        request->offset,
        data,
        request->length,
        &data_size,
        &eof);
    for(size_t cursor = 0u; sent && cursor < data_size;) {
        const size_t remaining = data_size - cursor;
        const size_t chunk = remaining > POISON_JS_BUNDLE_RPC_CHUNK_BYTES ?
                                 POISON_JS_BUNDLE_RPC_CHUNK_BYTES :
                                 remaining;
        PB_Main response = PB_Main_init_zero;
        response.command_id = command_id;
        response.command_status = PB_CommandStatus_OK;
        response.has_next = true;
        response.which_content = PB_Main_poison_js_bundle_frame_tag;
        PB_Poison_JsBundleFrame* frame = &response.content.poison_js_bundle_frame;
        frame->kind = PB_Poison_JsBundleFrameKind_JS_BUNDLE_FRAME_DATA;
        strcpy(frame->asset_path, asset->path);
        strcpy(frame->asset_sha256, asset->sha256);
        frame->asset_size = asset->size;
        frame->offset = request->offset + cursor;
        frame->data.size = chunk;
        memcpy(frame->data.bytes, data + cursor, chunk);
        frame->eof = eof && cursor + chunk == data_size;
        rpc_send_and_release(system->session, &response);
        cursor += chunk;
    }
    memset(data, 0, request->length);
    free(data);
    return sent && data_size > 0u;
}

static void rpc_poison_js_bundle_audit(
    RpcPoisonJsBundle* system,
    uint32_t command_id,
    const PB_Poison_JsBundleRequest* request,
    bool accepted) {
    uint8_t actor_digest[POISON_AUDIT_DIGEST_BYTES];
    uint8_t correlation_id[POISON_AUDIT_DIGEST_BYTES];
    if(!rpc_session_get_audit_context(system->session, command_id, actor_digest, correlation_id)) {
        return;
    }
    char metadata[POISON_AUDIT_METADATA_MAX + 1u];
    snprintf(
        metadata,
        sizeof(metadata),
        "id=%.32s;digest=%.16s;offset=%lu;length=%lu",
        request->bundle_id,
        request->content_sha256,
        (unsigned long)request->offset,
        (unsigned long)request->length);
    PoisonAuditEvent event;
    (void)poison_audit_append(
        poison_audit_get(),
        actor_digest,
        request->operation == PB_Poison_JsBundleOperation_JS_BUNDLE_OPERATION_DESCRIBE ?
            "js.bundle.describe" :
            "js.bundle.read",
        "js-bundle",
        accepted ? PoisonAuditDecisionAllowed : PoisonAuditDecisionDenied,
        ((uint64_t)furi_get_tick() * 1000u) / furi_kernel_get_tick_frequency(),
        correlation_id,
        metadata,
        &event);
    memset(actor_digest, 0, sizeof(actor_digest));
    memset(correlation_id, 0, sizeof(correlation_id));
    memset(&event, 0, sizeof(event));
}

static void rpc_poison_js_bundle_process(const PB_Main* request, void* context) {
    RpcPoisonJsBundle* system = context;
    if(!system || request->which_content != PB_Main_poison_js_bundle_request_tag ||
       request->has_next || !rpc_session_is_secure_dispatch_active(system->session)) {
        rpc_send_and_release_empty(
            system ? system->session : NULL,
            request->command_id,
            PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    const PB_Poison_JsBundleRequest* input = &request->content.poison_js_bundle_request;
    uint64_t session_id = 0u;
    PoisonRole role = PoisonRoleCount;
    PoisonCapability capabilities = 0u;
    bool accepted =
        rpc_poison_js_bundle_request_valid(input) &&
        rpc_session_get_secure_authorization(system->session, &session_id, &role, &capabilities) &&
        (capabilities & POISON_CAPABILITY_FILES) != 0u;
    UNUSED(session_id);
    UNUSED(role);
    const char* message = "rejected";
    if(accepted && input->operation == PB_Poison_JsBundleOperation_JS_BUNDLE_OPERATION_DESCRIBE) {
        if(!system->bundle) system->bundle = malloc(sizeof(*system->bundle));
        accepted = system->bundle && poison_js_bundle_load_verified(
                                         system->bundle,
                                         input->bundle_id,
                                         input->version,
                                         input->content_sha256,
                                         capabilities);
        if(accepted) {
            rpc_poison_js_bundle_send_inventory(system, request->command_id);
            message = "verified";
        }
    } else if(
        accepted &&
        input->operation == PB_Poison_JsBundleOperation_JS_BUNDLE_OPERATION_READ_ASSET) {
        accepted = rpc_poison_js_bundle_identity_matches(system->bundle, input) &&
                   poison_js_bundle_still_active(system->bundle) &&
                   rpc_poison_js_bundle_send_asset(system, input, request->command_id);
        if(accepted) message = "read";
    }
    rpc_poison_js_bundle_audit(system, request->command_id, input, accepted);
    rpc_poison_js_bundle_send_status(system, request->command_id, accepted, message);
}

void* rpc_system_poison_js_bundle_alloc(RpcSession* session) {
    furi_assert(session);
    RpcPoisonJsBundle* system = malloc(sizeof(*system));
    furi_check(system);
    memset(system, 0, sizeof(*system));
    system->session = session;
    RpcHandler handler = {
        .message_handler = rpc_poison_js_bundle_process,
        .decode_submessage = NULL,
        .context = system,
    };
    rpc_add_handler(session, PB_Main_poison_js_bundle_request_tag, &handler);
    return system;
}

void rpc_system_poison_js_bundle_free(void* context) {
    if(!context) return;
    RpcPoisonJsBundle* system = context;
    if(system->bundle) {
        memset(system->bundle, 0, sizeof(*system->bundle));
        free(system->bundle);
    }
    memset(system, 0, sizeof(*system));
    free(system);
}
