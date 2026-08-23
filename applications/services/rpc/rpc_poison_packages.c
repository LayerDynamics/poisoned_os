#include "rpc_poison_packages.h"
#include "rpc_i.h"

#include "../poison_packages/poison_package_archive.h"
#include "../poison_audit/poison_audit.h"
#include "../poison_diagnostics/poison_diagnostics.h"

#include <furi_hal_version.h>
#include <loader/firmware_api/firmware_api.h>
#include <mbedtls/sha256.h>
#include <storage/storage.h>

#include <string.h>

static bool rpc_poison_packages_confirmation_digest(
    const PB_Poison_PackageOperationRequest* request,
    const char* domain,
    uint8_t digest[POISON_CONFIRMATION_DIGEST_BYTES]) {
    if(!request || !domain || !digest) return false;
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool ok = mbedtls_sha256_starts(&hash, 0) == 0 &&
              mbedtls_sha256_update(&hash, (const uint8_t*)domain, strlen(domain) + 1u) == 0;
#define HASH_PACKAGE_FIELD(field)                                                                 \
    ok = ok &&                                                                                    \
         mbedtls_sha256_update(&hash, (const uint8_t*)&request->field, sizeof(request->field)) == \
             0
#define HASH_PACKAGE_STRING(field)    \
    ok = ok && mbedtls_sha256_update( \
                   &hash, (const uint8_t*)request->field, strlen(request->field) + 1u) == 0
    HASH_PACKAGE_FIELD(operation);
    HASH_PACKAGE_STRING(package_id);
    HASH_PACKAGE_STRING(version);
    HASH_PACKAGE_STRING(previous_version);
    HASH_PACKAGE_STRING(manifest_path);
    HASH_PACKAGE_STRING(candidate_digest);
    HASH_PACKAGE_STRING(previous_digest);
    HASH_PACKAGE_STRING(signing_key_id);
    HASH_PACKAGE_FIELD(capability_mask);
    HASH_PACKAGE_FIELD(release_sequence);
    HASH_PACKAGE_FIELD(received_bytes);
    HASH_PACKAGE_FIELD(content_bytes);
    HASH_PACKAGE_FIELD(previous_state);
    HASH_PACKAGE_FIELD(protected_package);
    HASH_PACKAGE_FIELD(confirmation_required);
    HASH_PACKAGE_FIELD(healthy);
#undef HASH_PACKAGE_STRING
#undef HASH_PACKAGE_FIELD
    ok = ok && mbedtls_sha256_finish(&hash, digest) == 0;
    mbedtls_sha256_free(&hash);
    return ok;
}

static bool rpc_poison_packages_issue_confirmation(
    RpcPoisonPackages* packages,
    const PB_Poison_PackageOperationRequest* request,
    const RpcPoisonPackagesRequestContext* request_context) {
    uint8_t command_digest[POISON_CONFIRMATION_DIGEST_BYTES];
    uint8_t target_digest[POISON_CONFIRMATION_DIGEST_BYTES];
    uint8_t consequence_digest[POISON_CONFIRMATION_DIGEST_BYTES];
    const bool issued =
        request_context && request_context->session_id != 0u &&
        request_context->role < PoisonRoleCount &&
        rpc_poison_packages_confirmation_digest(request, "package.command.v1", command_digest) &&
        rpc_poison_packages_confirmation_digest(request, "package.target.v1", target_digest) &&
        rpc_poison_packages_confirmation_digest(
            request, "package.consequence.v1", consequence_digest) &&
        poison_confirmation_issue(
            &packages->confirmation,
            request_context->session_id,
            request_context->role,
            command_digest,
            target_digest,
            consequence_digest,
            request_context->policy_version,
            request_context->now_ms,
            POISON_CONFIRMATION_MAX_TTL_MS,
            true) == PoisonConfirmationResultOk;
    memset(command_digest, 0, sizeof(command_digest));
    memset(target_digest, 0, sizeof(target_digest));
    memset(consequence_digest, 0, sizeof(consequence_digest));
    if(!issued) return false;
    memcpy(
        packages->confirmation_token,
        packages->confirmation.token,
        sizeof(packages->confirmation_token));
    strcpy(packages->confirmation_package_id, request->package_id);
    strcpy(packages->confirmation_digest, request->candidate_digest);
    packages->confirmation_operation = request->operation;
    packages->confirmation_capability_mask = request->capability_mask;
    packages->confirmation_active = true;
    return true;
}

static bool rpc_poison_packages_confirmation_matches(
    const RpcPoisonPackages* packages,
    const PB_Poison_PackageOperationRequest* request,
    const RpcPoisonPackagesRequestContext* request_context) {
    if(!packages->confirmation_active ||
       strcmp(packages->confirmation_package_id, request->package_id) != 0 ||
       strcmp(packages->confirmation_digest, request->candidate_digest) != 0 ||
       packages->confirmation_operation != request->operation ||
       packages->confirmation_capability_mask != request->capability_mask ||
       request->confirmation_token.size != sizeof(packages->confirmation_token)) {
        return false;
    }
    uint8_t command_digest[POISON_CONFIRMATION_DIGEST_BYTES];
    uint8_t target_digest[POISON_CONFIRMATION_DIGEST_BYTES];
    uint8_t consequence_digest[POISON_CONFIRMATION_DIGEST_BYTES];
    PoisonConfirmation probe = packages->confirmation;
    const bool matches =
        request_context &&
        rpc_poison_packages_confirmation_digest(request, "package.command.v1", command_digest) &&
        rpc_poison_packages_confirmation_digest(request, "package.target.v1", target_digest) &&
        rpc_poison_packages_confirmation_digest(
            request, "package.consequence.v1", consequence_digest) &&
        poison_confirmation_approve(
            &probe,
            request_context->session_id,
            request_context->role,
            command_digest,
            target_digest,
            consequence_digest,
            request->confirmation_token.bytes,
            request_context->policy_version,
            request_context->now_ms,
            true) == PoisonConfirmationResultOk;
    memset(&probe, 0, sizeof(probe));
    memset(command_digest, 0, sizeof(command_digest));
    memset(target_digest, 0, sizeof(target_digest));
    memset(consequence_digest, 0, sizeof(consequence_digest));
    return matches;
}

static void rpc_poison_packages_fill_status(
    const RpcPoisonPackages* packages,
    const char* package_id,
    const char* result,
    PB_Poison_PackageOperationStatus* status) {
    memset(status, 0, sizeof(*status));
    const PoisonPackageRecord* record = poison_package_manager_find(packages->manager, package_id);
    if(record) {
        strcpy(status->package_id, record->package_id);
        strcpy(status->version, record->version);
        strcpy(status->digest, record->digest);
        strcpy(status->signing_key_id, record->signing_key_id);
        status->state = (PB_Poison_PackageLifecycleState)record->transaction.state;
        status->capability_mask = record->capability_mask;
        status->received_bytes = record->transaction.content_update.received_bytes;
        status->content_bytes = record->transaction.content_update.content_bytes;
        status->protected_package = record->transaction.protected_package;
    }
    if(packages->confirmation_active &&
       strcmp(packages->confirmation_package_id, package_id) == 0) {
        status->confirmation_required = true;
        status->confirmation_token.size = sizeof(packages->confirmation_token);
        memcpy(
            status->confirmation_token.bytes,
            packages->confirmation_token,
            sizeof(packages->confirmation_token));
    }
    strcpy(status->result, result);
}

void rpc_poison_packages_init(RpcPoisonPackages* packages, PoisonPackageManager* manager) {
    if(!packages || !manager) return;
    memset(packages, 0, sizeof(*packages));
    packages->manager = manager;
}

void rpc_poison_packages_set_environment(
    RpcPoisonPackages* packages,
    uint32_t hardware_target,
    uint32_t firmware_api_major,
    uint32_t firmware_api_minor,
    uint64_t available_storage_bytes) {
    if(!packages) return;
    packages->hardware_target = hardware_target;
    packages->firmware_api_major = firmware_api_major;
    packages->firmware_api_minor = firmware_api_minor;
    packages->available_storage_bytes = available_storage_bytes;
}

void rpc_poison_packages_enable_storage(
    RpcPoisonPackages* packages,
    const PoisonPackageStorageLayout* layout) {
    if(!packages) return;
    if(layout) {
        packages->storage_layout = *layout;
    } else {
        poison_package_storage_layout_default(&packages->storage_layout);
    }
    packages->storage_enabled = true;
}

static bool rpc_poison_packages_require_confirmation(
    RpcPoisonPackages* packages,
    const PB_Poison_PackageOperationRequest* request,
    const RpcPoisonPackagesRequestContext* request_context) {
    if(!rpc_poison_packages_confirmation_matches(packages, request, request_context)) {
        (void)rpc_poison_packages_issue_confirmation(packages, request, request_context);
        return false;
    }
    uint8_t command_digest[POISON_CONFIRMATION_DIGEST_BYTES];
    uint8_t target_digest[POISON_CONFIRMATION_DIGEST_BYTES];
    uint8_t consequence_digest[POISON_CONFIRMATION_DIGEST_BYTES];
    const bool approved =
        request_context &&
        rpc_poison_packages_confirmation_digest(request, "package.command.v1", command_digest) &&
        rpc_poison_packages_confirmation_digest(request, "package.target.v1", target_digest) &&
        rpc_poison_packages_confirmation_digest(
            request, "package.consequence.v1", consequence_digest) &&
        poison_confirmation_approve(
            &packages->confirmation,
            request_context->session_id,
            request_context->role,
            command_digest,
            target_digest,
            consequence_digest,
            request->confirmation_token.bytes,
            request_context->policy_version,
            request_context->now_ms,
            request_context->physical_confirmed) == PoisonConfirmationResultOk;
    memset(command_digest, 0, sizeof(command_digest));
    memset(target_digest, 0, sizeof(target_digest));
    memset(consequence_digest, 0, sizeof(consequence_digest));
    if(!approved) return false;
    packages->confirmation_active = false;
    memset(packages->confirmation_token, 0, sizeof(packages->confirmation_token));
    packages->confirmation_package_id[0] = '\0';
    packages->confirmation_digest[0] = '\0';
    packages->confirmation_capability_mask = 0u;
    memset(&packages->confirmation, 0, sizeof(packages->confirmation));
    return true;
}

bool rpc_poison_packages_process(
    RpcPoisonPackages* packages,
    const PB_Poison_PackageOperationRequest* request,
    const PoisonPackageVerifiedArchive* verified_archive,
    const RpcPoisonPackagesRequestContext* request_context,
    PB_Poison_PackageOperationStatus* status) {
    if(!packages || !request || !status || request->package_id[0] == '\0') return false;
    PoisonPackageManager* previous_manager = NULL;
    if(packages->persistence_enabled) {
        previous_manager = malloc(sizeof(*previous_manager));
        if(!previous_manager) {
            rpc_poison_packages_fill_status(
                packages, request->package_id, "out-of-memory", status);
            return false;
        }
        *previous_manager = *packages->manager;
    }
    bool result = false;
    const char* result_name = "invalid";

    switch(request->operation) {
    case PB_Poison_PackageOperation_PACKAGE_OPERATION_INSPECT:
        result = poison_package_manager_find(packages->manager, request->package_id) != NULL;
        result_name = result ? "inspected" : "not-found";
        break;
    case PB_Poison_PackageOperation_PACKAGE_OPERATION_IMPORT: {
        PoisonContentUpdateType update_type;
        if(!verified_archive ||
           !poison_package_content_type_parse(verified_archive->content_type, &update_type) ||
           strcmp(verified_archive->package_id, request->package_id) != 0 ||
           strcmp(verified_archive->archive_sha256, request->candidate_digest) != 0) {
            result_name = "manifest-rejected";
            break;
        }
        if(request->previous_state >
           PB_Poison_PackageLifecycleState_PACKAGE_LIFECYCLE_STATE_REMOVED) {
            break;
        }
        const PoisonPackageImport package = {
            .content_type = verified_archive->content_type,
            .package_id = request->package_id,
            .version = verified_archive->version,
            .previous_version = request->previous_version,
            .candidate_digest = verified_archive->archive_sha256,
            .previous_digest = request->previous_digest,
            .signing_key_id = verified_archive->signing_key_id,
            .manifest_path = request->manifest_path,
            .entrypoint = verified_archive->entrypoint,
            .capability_mask = verified_archive->capability_mask,
            .release_sequence = verified_archive->release_sequence,
            .content_bytes = verified_archive->archive_bytes,
            .hardware_target = packages->hardware_target,
            .firmware_api = packages->firmware_api_major,
            .available_storage_bytes = packages->available_storage_bytes,
            .previous_state = (PoisonPackageState)request->previous_state,
            .protected_package =
                strcmp(verified_archive->package_id, "org.poisonedos.recovery") == 0 ||
                strncmp(verified_archive->package_id, "org.poisonedos.core.", 20u) == 0,
            .confirmation_required =
                (verified_archive->capability_mask & ((1u << 5) | (1u << 6) | (1u << 7))) != 0u,
            .manifest_verified = true,
        };
        result = poison_package_manager_import(packages->manager, &package);
        result_name = result ? "imported" : "manifest-rejected";
        break;
    }
    case PB_Poison_PackageOperation_PACKAGE_OPERATION_STAGE:
        result = poison_package_manager_receive(
            packages->manager, request->package_id, request->received_bytes);
        result_name = result ? "staged" : "stage-rejected";
        break;
    case PB_Poison_PackageOperation_PACKAGE_OPERATION_VERIFY:
        result = verified_archive != NULL;
        if(result && packages->storage_enabled) {
            const PoisonPackageRecord* record =
                poison_package_manager_find(packages->manager, request->package_id);
            result = record &&
                     poison_package_storage_stage(
                         &packages->storage_layout, record->manifest_path, verified_archive);
        }
        result = poison_package_manager_verify(
            packages->manager, request->package_id, request->candidate_digest, result);
        if(!result && packages->storage_enabled && verified_archive) {
            (void)poison_package_storage_quarantine(
                &packages->storage_layout, request->package_id);
        }
        if(result) {
            PB_Poison_PackageOperationRequest confirmation = *request;
            confirmation.operation = PB_Poison_PackageOperation_PACKAGE_OPERATION_ACTIVATE;
            result =
                rpc_poison_packages_issue_confirmation(packages, &confirmation, request_context);
        }
        result_name = result ? "verified" : "verification-failed";
        break;
    case PB_Poison_PackageOperation_PACKAGE_OPERATION_ACTIVATE:
        if(!rpc_poison_packages_require_confirmation(packages, request, request_context)) {
            result_name = "confirmation-required";
            break;
        }
        result = !packages->storage_enabled ||
                 poison_package_storage_activate(&packages->storage_layout, request->package_id);
        if(result) {
            result = poison_package_manager_activate(packages->manager, request->package_id, true);
            if(!result && packages->storage_enabled) {
                (void)poison_package_storage_report_health(
                    &packages->storage_layout, request->package_id, false);
            }
        }
        result_name = result ? "activating" : "activation-rejected";
        break;
    case PB_Poison_PackageOperation_PACKAGE_OPERATION_HEALTH:
        result = !packages->storage_enabled ||
                 poison_package_storage_report_health(
                     &packages->storage_layout, request->package_id, request->healthy);
        if(result) {
            result = poison_package_manager_report_health(
                packages->manager, request->package_id, request->healthy);
        }
        result_name = result ? (request->healthy ? "healthy" : "rolled-back") : "health-rejected";
        break;
    case PB_Poison_PackageOperation_PACKAGE_OPERATION_DISABLE:
        result = poison_package_manager_set_enabled(
            packages->manager, request->package_id, false, false);
        result_name = result ? "disabled" : "disable-rejected";
        break;
    case PB_Poison_PackageOperation_PACKAGE_OPERATION_ENABLE:
    case PB_Poison_PackageOperation_PACKAGE_OPERATION_REMOVE:
    case PB_Poison_PackageOperation_PACKAGE_OPERATION_ROLLBACK:
        if(!rpc_poison_packages_require_confirmation(packages, request, request_context)) {
            result_name = "confirmation-required";
            break;
        }
        if(request->operation == PB_Poison_PackageOperation_PACKAGE_OPERATION_ENABLE) {
            result = poison_package_manager_set_enabled(
                packages->manager, request->package_id, true, true);
            result_name = result ? "enabled" : "enable-rejected";
        } else if(request->operation == PB_Poison_PackageOperation_PACKAGE_OPERATION_REMOVE) {
            result = !packages->storage_enabled ||
                     poison_package_storage_remove(&packages->storage_layout, request->package_id);
            if(result) {
                result =
                    poison_package_manager_remove(packages->manager, request->package_id, true);
                if(!result && packages->storage_enabled) {
                    (void)poison_package_storage_restore_removed(
                        &packages->storage_layout, request->package_id);
                }
            }
            result_name = result ? "removed" : "remove-rejected";
        } else {
            result =
                !packages->storage_enabled ||
                poison_package_storage_rollback(&packages->storage_layout, request->package_id);
            if(result) {
                result =
                    poison_package_manager_rollback(packages->manager, request->package_id, true);
                if(!result && packages->storage_enabled) {
                    (void)poison_package_storage_rollback(
                        &packages->storage_layout, request->package_id);
                }
            }
            result_name = result ? "rolled-back" : "rollback-rejected";
        }
        break;
    case PB_Poison_PackageOperation_PACKAGE_OPERATION_QUARANTINE:
        result =
            poison_package_manager_find(packages->manager, request->package_id) != NULL &&
            (!packages->storage_enabled ||
             poison_package_storage_quarantine(&packages->storage_layout, request->package_id));
        if(result) {
            result = poison_package_manager_quarantine(packages->manager, request->package_id);
        }
        result_name = result ? "quarantined" : "quarantine-rejected";
        break;
    default:
        break;
    }

    const bool operation_succeeded = result;
    const bool manager_changed = previous_manager &&
                                 previous_manager->generation != packages->manager->generation;
    if(manager_changed && packages->persistence_enabled &&
       request->operation != PB_Poison_PackageOperation_PACKAGE_OPERATION_INSPECT &&
       !poison_packages_save_state()) {
        if(operation_succeeded && packages->storage_enabled) {
            switch(request->operation) {
            case PB_Poison_PackageOperation_PACKAGE_OPERATION_VERIFY:
                (void)poison_package_storage_quarantine(
                    &packages->storage_layout, request->package_id);
                break;
            case PB_Poison_PackageOperation_PACKAGE_OPERATION_ACTIVATE:
                (void)poison_package_storage_report_health(
                    &packages->storage_layout, request->package_id, false);
                break;
            case PB_Poison_PackageOperation_PACKAGE_OPERATION_HEALTH:
                if(!request->healthy) {
                    (void)poison_package_storage_revert_health_rollback(
                        &packages->storage_layout, request->package_id);
                }
                break;
            case PB_Poison_PackageOperation_PACKAGE_OPERATION_REMOVE:
                (void)poison_package_storage_restore_removed(
                    &packages->storage_layout, request->package_id);
                break;
            case PB_Poison_PackageOperation_PACKAGE_OPERATION_ROLLBACK:
                (void)poison_package_storage_rollback(
                    &packages->storage_layout, request->package_id);
                break;
            case PB_Poison_PackageOperation_PACKAGE_OPERATION_QUARANTINE: {
                const PoisonPackageRecord* prior =
                    poison_package_manager_find(previous_manager, request->package_id);
                const bool candidate = prior &&
                                       (prior->transaction.state == PoisonPackageStaged ||
                                        prior->transaction.state == PoisonPackageVerified);
                (void)poison_package_storage_restore_quarantine(
                    &packages->storage_layout, request->package_id, candidate);
                break;
            }
            default:
                break;
            }
        }
        *packages->manager = *previous_manager;
        result = false;
        result_name = "state-persist-failed";
    }

    rpc_poison_packages_fill_status(packages, request->package_id, result_name, status);
    if(previous_manager) {
        memset(previous_manager, 0, sizeof(*previous_manager));
        free(previous_manager);
    }
    return result;
}

typedef struct {
    RpcSession* session;
    RpcPoisonPackages packages;
} RpcPoisonPackagesSystem;

static void rpc_poison_packages_operation_process(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(context);
    RpcPoisonPackagesSystem* system = context;
    if(request->which_content != PB_Main_poison_package_operation_request_tag ||
       request->has_next || !rpc_session_is_secure_dispatch_active(system->session)) {
        rpc_send_and_release_empty(
            system->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }

    const PB_Poison_PackageOperationRequest* operation =
        &request->content.poison_package_operation_request;
    PoisonPackageVerifiedArchive* verified = NULL;
    PoisonPackageArchiveResult verification = PoisonPackageArchiveInvalid;
    bool verification_attempted = false;
    if(operation->operation == PB_Poison_PackageOperation_PACKAGE_OPERATION_IMPORT ||
       operation->operation == PB_Poison_PackageOperation_PACKAGE_OPERATION_VERIFY) {
        const PoisonPackageRecord* installed =
            poison_package_manager_find(system->packages.manager, operation->package_id);
        verified = malloc(sizeof(*verified));
        if(verified) {
            const char* path = operation->operation ==
                                       PB_Poison_PackageOperation_PACKAGE_OPERATION_IMPORT ?
                                   operation->manifest_path :
                                   (installed ? installed->manifest_path : NULL);
            const char* expected = operation->candidate_digest;
            const char* installed_version = installed ? installed->version : NULL;
            verification_attempted = true;
            verification = poison_package_verify_archive(
                path,
                expected,
                poison_packages_authorities(),
                system->packages.firmware_api_major,
                system->packages.firmware_api_minor,
                installed_version,
                verified);
            const bool request_matches =
                verification == PoisonPackageArchiveOk &&
                strcmp(verified->package_id, operation->package_id) == 0 &&
                strcmp(verified->archive_sha256, operation->candidate_digest) == 0;
            if(operation->operation == PB_Poison_PackageOperation_PACKAGE_OPERATION_IMPORT) {
                if(!request_matches) {
                    memset(verified, 0, sizeof(*verified));
                    free(verified);
                    verified = NULL;
                }
            } else {
                if(!request_matches || !installed ||
                   strcmp(verified->signing_key_id, installed->signing_key_id) != 0) {
                    memset(verified, 0, sizeof(*verified));
                    free(verified);
                    verified = NULL;
                }
            }
        }
    }

    RpcPoisonPackagesRequestContext request_context = {
        .policy_version = 1u,
        .now_ms = ((uint64_t)furi_get_tick() * 1000u) / furi_kernel_get_tick_frequency(),
    };
    if(!rpc_session_get_secure_identity(
           system->session, &request_context.session_id, &request_context.role)) {
        if(verified) {
            memset(verified, 0, sizeof(*verified));
            free(verified);
        }
        rpc_send_and_release_empty(
            system->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    switch(operation->operation) {
    case PB_Poison_PackageOperation_PACKAGE_OPERATION_ACTIVATE:
    case PB_Poison_PackageOperation_PACKAGE_OPERATION_ENABLE:
    case PB_Poison_PackageOperation_PACKAGE_OPERATION_REMOVE:
    case PB_Poison_PackageOperation_PACKAGE_OPERATION_ROLLBACK:
        if(rpc_poison_packages_confirmation_matches(
               &system->packages, operation, &request_context)) {
            request_context.physical_confirmed = rpc_session_request_content_update_confirmation(
                system->session,
                operation->package_id,
                operation->candidate_digest,
                PoisonContentUpdateApplication);
        }
        break;
    default:
        break;
    }
    request_context.now_ms =
        ((uint64_t)furi_get_tick() * 1000u) / furi_kernel_get_tick_frequency();

    PB_Main response = PB_Main_init_zero;
    response.command_id = request->command_id;
    response.command_status = PB_CommandStatus_OK;
    response.which_content = PB_Main_poison_package_operation_status_tag;
    const bool operation_succeeded = rpc_poison_packages_process(
        &system->packages,
        operation,
        verified,
        &request_context,
        &response.content.poison_package_operation_status);
    if(verified) {
        memset(verified, 0, sizeof(*verified));
        free(verified);
    }
    if(response.content.poison_package_operation_status.result[0] == '\0') {
        rpc_send_and_release_empty(
            system->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    static const char* const audit_actions[] = {
        "package.inspect",
        "package.import",
        "package.stage",
        "package.verify",
        "package.activate",
        "package.health",
        "package.disable",
        "package.enable",
        "package.remove",
        "package.rollback",
        "package.quarantine",
    };
    if((size_t)operation->operation < COUNT_OF(audit_actions)) {
        uint8_t actor_digest[POISON_AUDIT_DIGEST_BYTES];
        uint8_t correlation_id[POISON_AUDIT_DIGEST_BYTES];
        if(rpc_session_get_audit_context(
               system->session, request->command_id, actor_digest, correlation_id)) {
            PoisonAuditEvent event;
            const uint64_t now_ms =
                ((uint64_t)furi_get_tick() * 1000u) / furi_kernel_get_tick_frequency();
            char safe_metadata[POISON_AUDIT_METADATA_MAX + 1u];
            snprintf(
                safe_metadata,
                sizeof(safe_metadata),
                "id=%.24s;result=%.28s",
                operation->package_id,
                response.content.poison_package_operation_status.result);
            (void)poison_audit_append(
                poison_audit_get(),
                actor_digest,
                audit_actions[operation->operation],
                "package",
                operation_succeeded ? PoisonAuditDecisionAllowed : PoisonAuditDecisionDenied,
                now_ms,
                correlation_id,
                safe_metadata,
                &event);
            if(verification_attempted) {
                (void)poison_diagnostics_record(
                    poison_diagnostics_get(),
                    PoisonDiagnosticPackageVerification,
                    verification == PoisonPackageArchiveOk ? "package accepted" :
                                                             "package rejected",
                    now_ms,
                    correlation_id);
                if(verification == PoisonPackageArchiveRevokedSigner) {
                    (void)poison_diagnostics_record(
                        poison_diagnostics_get(),
                        PoisonDiagnosticPackageRevocation,
                        "signer revoked",
                        now_ms,
                        correlation_id);
                }
            }
            memset(actor_digest, 0, sizeof(actor_digest));
            memset(correlation_id, 0, sizeof(correlation_id));
            memset(&event, 0, sizeof(event));
        }
    }
    rpc_send_and_release(system->session, &response);
}

void* rpc_system_poison_packages_alloc(RpcSession* session) {
    furi_assert(session);
    RpcPoisonPackagesSystem* system = malloc(sizeof(*system));
    furi_check(system);
    memset(system, 0, sizeof(*system));
    system->session = session;
    rpc_poison_packages_init(&system->packages, poison_packages_manager());
    uint64_t total_space = 0u;
    uint64_t free_space = 0u;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(storage_common_fs_info(storage, "/ext", &total_space, &free_space) != FSE_OK)
        free_space = 0u;
    furi_record_close(RECORD_STORAGE);
    rpc_poison_packages_set_environment(
        &system->packages,
        furi_hal_version_get_hw_target(),
        firmware_api_interface->api_version_major,
        firmware_api_interface->api_version_minor,
        free_space);
    rpc_poison_packages_enable_storage(&system->packages, NULL);
    system->packages.persistence_enabled = true;
    RpcHandler handler = {
        .message_handler = rpc_poison_packages_operation_process,
        .decode_submessage = NULL,
        .context = system,
    };
    rpc_add_handler(session, PB_Main_poison_package_operation_request_tag, &handler);
    return system;
}

void rpc_system_poison_packages_free(void* context) {
    if(!context) return;
    RpcPoisonPackagesSystem* system = context;
    memset(system, 0, sizeof(*system));
    free(system);
}
