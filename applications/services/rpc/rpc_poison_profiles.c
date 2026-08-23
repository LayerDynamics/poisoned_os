#include "rpc_i.h"

#include "../poison_profiles/poison_profile_assets.h"
#include "../poison_profiles/poison_profiles_i.h"
#include "../poison_audit/poison_audit.h"
#include "poison_confirmation.h"
#include "poison_policy.h"

#include <mbedtls/sha256.h>

#include <stdio.h>
#include <string.h>

typedef struct {
    RpcSession* session;
    PoisonConfirmation confirmation;
    char preview_id[POISON_PROFILE_TEXT];
} RpcPoisonProfiles;

static void rpc_poison_profile_audit(
    RpcPoisonProfiles* profiles,
    uint32_t command_id,
    const char* action,
    const char* profile_id,
    bool allowed) {
    uint8_t actor[POISON_AUDIT_DIGEST_BYTES];
    uint8_t correlation[POISON_AUDIT_DIGEST_BYTES];
    if(rpc_session_get_audit_context(profiles->session, command_id, actor, correlation)) {
        PoisonAuditEvent event;
        char metadata[POISON_AUDIT_METADATA_MAX + 1u];
        snprintf(
            metadata,
            sizeof(metadata),
            "id=%.32s;result=%s",
            profile_id ? profile_id : "invalid",
            allowed ? "accepted" : "rejected");
        (void)poison_audit_append(
            poison_audit_get(),
            actor,
            action,
            "profile",
            allowed ? PoisonAuditDecisionAllowed : PoisonAuditDecisionDenied,
            ((uint64_t)furi_get_tick() * 1000u) / furi_kernel_get_tick_frequency(),
            correlation,
            metadata,
            &event);
        memset(&event, 0, sizeof(event));
    }
    memset(actor, 0, sizeof(actor));
    memset(correlation, 0, sizeof(correlation));
}

bool rpc_poison_profile_apply_is_bounded(const char* profile_id, const char* confirmation_token) {
    return profile_id && confirmation_token && profile_id[0] != '\0' &&
           strnlen(profile_id, 65u) < 65u && strnlen(confirmation_token, 129u) < 129u;
}

static bool rpc_poison_profile_from_pb(const PB_Poison_Profile* input, PoisonProfile* profile) {
    if(!input || !profile || input->enabled_tools_count > POISON_PROFILE_MAX_LIST ||
       input->favorites_count > POISON_PROFILE_MAX_LIST ||
       input->hidden_tools_count > POISON_PROFILE_MAX_LIST ||
       input->shortcuts_count > POISON_PROFILE_MAX_LIST) {
        return false;
    }
    memset(profile, 0, sizeof(*profile));
    profile->format = input->format;
    strcpy(profile->id, input->id);
    strcpy(profile->version, input->version);
    strcpy(profile->role, input->role);
    profile->enabled_tools_count = input->enabled_tools_count;
    for(size_t index = 0u; index < profile->enabled_tools_count; index++) {
        strcpy(profile->enabled_tools[index], input->enabled_tools[index]);
    }
    profile->favorites_count = input->favorites_count;
    for(size_t index = 0u; index < profile->favorites_count; index++) {
        strcpy(profile->favorites[index], input->favorites[index]);
    }
    profile->hidden_tools_count = input->hidden_tools_count;
    for(size_t index = 0u; index < profile->hidden_tools_count; index++) {
        strcpy(profile->hidden_tools[index], input->hidden_tools[index]);
    }
    profile->shortcuts_count = input->shortcuts_count;
    for(size_t index = 0u; index < profile->shortcuts_count; index++) {
        strcpy(profile->shortcuts[index], input->shortcuts[index]);
    }
    strcpy(profile->policy_id, input->policy_id);
    strcpy(profile->theme_id, input->theme_id);
    strcpy(profile->font_pack_id, input->font_pack_id);
    strcpy(profile->icon_pack_id, input->icon_pack_id);
    strcpy(profile->menu_id, input->menu_id);
    strcpy(profile->dashboard_layout, input->dashboard_layout);
    strcpy(profile->home_presentation, input->home_presentation);
    strcpy(profile->status_presentation, input->status_presentation);
    strcpy(profile->lock_behavior, input->lock_behavior);
    strcpy(profile->tool_defaults_json, input->tool_defaults_json);
    strcpy(profile->transport_policy, input->transport_policy);
    strcpy(profile->logging_policy, input->logging_policy);
    strcpy(profile->evidence_policy, input->evidence_policy);
    strcpy(profile->radio_region, input->radio_region);
    strcpy(profile->peripheral_safety, input->peripheral_safety);
    strcpy(profile->classroom_policy, input->classroom_policy);
    profile->contrast_ratio_x10 = input->contrast_ratio_x10;
    profile->capability_mask = input->capability_mask;
    profile->notifications_enabled = input->notifications_enabled;
    profile->haptics_enabled = input->haptics_enabled;
    profile->classroom_restricted = input->classroom_restricted;
    profile->known_good = false;
    bool lists_valid = true;
    for(size_t index = 0u; lists_valid && index < profile->enabled_tools_count; index++) {
        lists_valid = poison_profile_asset_identifier_valid(profile->enabled_tools[index], 64u);
    }
    for(size_t index = 0u; lists_valid && index < profile->favorites_count; index++) {
        lists_valid = poison_profile_asset_identifier_valid(profile->favorites[index], 64u);
    }
    for(size_t index = 0u; lists_valid && index < profile->hidden_tools_count; index++) {
        lists_valid = poison_profile_asset_identifier_valid(profile->hidden_tools[index], 64u);
    }
    for(size_t index = 0u; lists_valid && index < profile->shortcuts_count; index++) {
        lists_valid = poison_profile_asset_identifier_valid(profile->shortcuts[index], 64u);
    }
    return lists_valid && poison_profile_asset_identifier_valid(profile->id, 64u) &&
           poison_profile_asset_identifier_valid(profile->policy_id, 64u) &&
           poison_profile_asset_identifier_valid(profile->theme_id, 64u) &&
           poison_profile_asset_identifier_valid(profile->font_pack_id, 64u) &&
           poison_profile_asset_identifier_valid(profile->icon_pack_id, 64u) &&
           poison_profile_asset_identifier_valid(profile->menu_id, 64u);
}

static bool rpc_poison_profile_digest(
    const PoisonProfile* profile,
    const char* domain,
    uint8_t digest[POISON_CONFIRMATION_DIGEST_BYTES]) {
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    const bool ok = profile && domain && mbedtls_sha256_starts(&hash, 0) == 0 &&
                    mbedtls_sha256_update(&hash, (const uint8_t*)domain, strlen(domain) + 1u) ==
                        0 &&
                    mbedtls_sha256_update(&hash, (const uint8_t*)profile, sizeof(*profile)) == 0 &&
                    mbedtls_sha256_finish(&hash, digest) == 0;
    mbedtls_sha256_free(&hash);
    return ok;
}

static bool rpc_poison_profile_digests(
    const PoisonProfile* profile,
    uint8_t command[POISON_CONFIRMATION_DIGEST_BYTES],
    uint8_t target[POISON_CONFIRMATION_DIGEST_BYTES],
    uint8_t consequence[POISON_CONFIRMATION_DIGEST_BYTES]) {
    return rpc_poison_profile_digest(profile, "profile.apply.command.v1", command) &&
           rpc_poison_profile_digest(profile, "profile.apply.target.v1", target) &&
           rpc_poison_profile_digest(profile, "profile.apply.consequence.v1", consequence);
}

static void rpc_poison_profile_status(
    PB_Main* response,
    uint32_t command_id,
    const PoisonProfile* profile,
    const PoisonConfirmation* confirmation,
    bool preview) {
    *response = (PB_Main)PB_Main_init_zero;
    response->command_id = command_id;
    response->command_status = PB_CommandStatus_OK;
    response->which_content = PB_Main_poison_profile_status_tag;
    PB_Poison_ProfileStatus* status = &response->content.poison_profile_status;
    strcpy(status->profile_id, profile->id);
    strcpy(status->version, profile->version);
    status->known_good = profile->known_good;
    status->preview = preview;
    if(confirmation && confirmation->active) {
        status->confirmation_token.size = POISON_CONFIRMATION_TOKEN_BYTES;
        memcpy(
            status->confirmation_token.bytes,
            confirmation->token,
            POISON_CONFIRMATION_TOKEN_BYTES);
    }
}

static void rpc_poison_profile_preview_process(const PB_Main* request, void* context) {
    RpcPoisonProfiles* profiles = context;
    if(request->which_content != PB_Main_poison_profile_tag || request->has_next ||
       !rpc_session_is_secure_dispatch_active(profiles->session)) {
        rpc_send_and_release_empty(
            profiles->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    uint64_t session_id;
    PoisonRole role;
    PoisonProfile profile;
    uint8_t command[POISON_CONFIRMATION_DIGEST_BYTES];
    uint8_t target[POISON_CONFIRMATION_DIGEST_BYTES];
    uint8_t consequence[POISON_CONFIRMATION_DIGEST_BYTES];
    const uint64_t now_ms = ((uint64_t)furi_get_tick() * 1000u) / furi_kernel_get_tick_frequency();
    const bool previewed =
        rpc_session_get_secure_identity(profiles->session, &session_id, &role) &&
        rpc_poison_profile_from_pb(&request->content.poison_profile, &profile) &&
        poison_profiles_preview_global(&profile, poison_policy_role_capabilities(role)) &&
        rpc_poison_profile_digests(&profile, command, target, consequence) &&
        poison_confirmation_issue(
            &profiles->confirmation,
            session_id,
            role,
            command,
            target,
            consequence,
            1u,
            now_ms,
            POISON_CONFIRMATION_MAX_TTL_MS,
            true) == PoisonConfirmationResultOk;
    memset(command, 0, sizeof(command));
    memset(target, 0, sizeof(target));
    memset(consequence, 0, sizeof(consequence));
    rpc_poison_profile_audit(
        profiles,
        request->command_id,
        "profile.preview",
        request->content.poison_profile.id,
        previewed);
    if(!previewed) {
        memset(&profile, 0, sizeof(profile));
        rpc_send_and_release_empty(
            profiles->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    strcpy(profiles->preview_id, profile.id);
    PB_Main* response = malloc(sizeof(PB_Main));
    furi_check(response);
    rpc_poison_profile_status(
        response, request->command_id, &profile, &profiles->confirmation, true);
    memset(&profile, 0, sizeof(profile));
    rpc_send_and_release(profiles->session, response);
    free(response);
}

static void rpc_poison_profile_apply_process(const PB_Main* request, void* context) {
    RpcPoisonProfiles* profiles = context;
    if(request->which_content != PB_Main_poison_profile_apply_tag || request->has_next ||
       !rpc_session_is_secure_dispatch_active(profiles->session)) {
        rpc_send_and_release_empty(
            profiles->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    const PB_Poison_ProfileApply* apply = &request->content.poison_profile_apply;
    PoisonProfile active;
    PoisonProfile preview;
    bool preview_valid = false;
    uint64_t session_id;
    PoisonRole role;
    uint8_t command[POISON_CONFIRMATION_DIGEST_BYTES];
    uint8_t target[POISON_CONFIRMATION_DIGEST_BYTES];
    uint8_t consequence[POISON_CONFIRMATION_DIGEST_BYTES];
    const bool prepared =
        rpc_poison_profile_apply_is_bounded(apply->profile_id, apply->confirmation_token) &&
        apply->confirmation_token[0] == '\0' &&
        apply->confirmation_token_bytes.size == POISON_CONFIRMATION_TOKEN_BYTES &&
        strcmp(apply->profile_id, profiles->preview_id) == 0 &&
        poison_profiles_copy_global(&active, &preview, &preview_valid) && preview_valid &&
        strcmp(preview.id, apply->profile_id) == 0 &&
        rpc_session_get_secure_identity(profiles->session, &session_id, &role) &&
        rpc_poison_profile_digests(&preview, command, target, consequence);
    const bool physical =
        prepared && rpc_session_request_profile_confirmation(
                        profiles->session, preview.id, preview.version, preview.capability_mask);
    const uint64_t approval_now_ms =
        ((uint64_t)furi_get_tick() * 1000u) / furi_kernel_get_tick_frequency();
    const bool approved = physical && poison_confirmation_approve(
                                          &profiles->confirmation,
                                          session_id,
                                          role,
                                          command,
                                          target,
                                          consequence,
                                          apply->confirmation_token_bytes.bytes,
                                          1u,
                                          approval_now_ms,
                                          true) == PoisonConfirmationResultOk;
    const bool applied = approved && poison_profiles_apply_global();
    rpc_poison_profile_audit(
        profiles, request->command_id, "profile.apply", apply->profile_id, applied);
    memset(command, 0, sizeof(command));
    memset(target, 0, sizeof(target));
    memset(consequence, 0, sizeof(consequence));
    memset(&active, 0, sizeof(active));
    memset(&preview, 0, sizeof(preview));
    profiles->preview_id[0] = '\0';
    if(!applied || !poison_profiles_copy_global(&active, NULL, NULL)) {
        rpc_send_and_release_empty(
            profiles->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    PB_Main* response = malloc(sizeof(PB_Main));
    furi_check(response);
    rpc_poison_profile_status(response, request->command_id, &active, NULL, false);
    memset(&active, 0, sizeof(active));
    rpc_send_and_release(profiles->session, response);
    free(response);
}

void* rpc_system_poison_profiles_alloc(RpcSession* session) {
    RpcPoisonProfiles* profiles = malloc(sizeof(*profiles));
    furi_check(profiles);
    memset(profiles, 0, sizeof(*profiles));
    profiles->session = session;
    RpcHandler handler = {
        .message_handler = rpc_poison_profile_preview_process,
        .decode_submessage = NULL,
        .context = profiles,
    };
    rpc_add_handler(session, PB_Main_poison_profile_tag, &handler);
    handler.message_handler = rpc_poison_profile_apply_process;
    rpc_add_handler(session, PB_Main_poison_profile_apply_tag, &handler);
    return profiles;
}

void rpc_system_poison_profiles_free(void* context) {
    if(!context) return;
    RpcPoisonProfiles* profiles = context;
    memset(profiles, 0, sizeof(*profiles));
    free(profiles);
}
