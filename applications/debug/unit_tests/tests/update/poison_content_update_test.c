#include "../test.h"
#include "../../../../services/poison_packages/poison_content_update.h"
#include "../../../../services/poison_packages/poison_content_update_internal.h"
#include "../../../../services/rpc/rpc_poison_content_update.h"

#include <furi.h>
#include <storage/storage.h>

static const PoisonContentUpdateManifest valid_manifest = {
    .content_type = PoisonContentUpdateFirmware,
    .update_id = "firmware-2",
    .candidate_digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
    .previous_digest = "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd",
    .hardware_target = 7u,
    .minimum_api = 88u,
    .maximum_api = 89u,
    .release_sequence = 2u,
    .content_bytes = 8192u,
    .signature_valid = true,
    .signer_revoked = false,
    .rollback_available = true,
    .protected_target = true,
};

static const PoisonContentUpdateEnvironment valid_environment = {
    .hardware_target = 7u,
    .firmware_api = 88u,
    .highest_release_sequence = 1u,
    .available_storage_bytes = 16384u,
};

static PoisonContentUpdateAdmission admit(
    const PoisonContentUpdateManifest* manifest,
    const PoisonContentUpdateEnvironment* environment) {
    PoisonContentUpdate update;
    return poison_content_update_admit(&update, manifest, environment);
}

static bool poison_content_update_test_activate(void* context, const char* manifest_path) {
    bool* called = context;
    *called = strcmp(manifest_path, "/ext/update/poison/update.poison") == 0;
    return *called;
}

static bool poison_content_update_test_verify(
    void* context,
    const char* manifest_path,
    const char* candidate_digest,
    PoisonPackageVerifiedArchive* verified) {
    bool* called = context;
    *called = strcmp(manifest_path, "/ext/update/poison/update.poison") == 0 &&
              strcmp(candidate_digest, valid_manifest.candidate_digest) == 0;
    if(*called) {
        memset(verified, 0, sizeof(*verified));
        strcpy(verified->content_type, "firmware");
        strcpy(verified->package_id, valid_manifest.update_id);
        strcpy(verified->version, "2.0.0");
        strcpy(verified->entrypoint, "update.fuf");
        strcpy(verified->archive_sha256, valid_manifest.candidate_digest);
        verified->release_sequence = valid_manifest.release_sequence;
        verified->archive_bytes = valid_manifest.content_bytes;
        verified->payload_count = 1u;
        strcpy(verified->payloads[0].path, "update.fuf");
        strcpy(
            verified->payloads[0].sha256,
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        verified->payloads[0].size = 1u;
    }
    return *called;
}

typedef struct {
    const char* expected_path;
    bool called;
} PoisonContentUpdateRollbackFixture;

static bool poison_content_update_test_rollback(void* context, const char* manifest_path) {
    PoisonContentUpdateRollbackFixture* fixture = context;
    fixture->called = strcmp(manifest_path, fixture->expected_path) == 0;
    return fixture->called;
}

MU_TEST(poison_content_update_admits_exact_signed_contract) {
    PoisonContentUpdate update;
    mu_check(
        poison_content_update_admit(&update, &valid_manifest, &valid_environment) ==
        PoisonContentUpdateAdmissionOk);
    mu_check(update.state == PoisonContentUpdateDiscovered);
    mu_check(update.content_type == PoisonContentUpdateFirmware);
    mu_check(update.sequence == valid_manifest.release_sequence);
    mu_check(update.content_bytes == valid_manifest.content_bytes);
    mu_check(update.rollback_available);
    mu_check(update.confirmation_required);
}

MU_TEST(poison_content_update_rejects_untrusted_or_incompatible_contracts) {
    PoisonContentUpdateManifest manifest = valid_manifest;
    PoisonContentUpdateEnvironment environment = valid_environment;

    manifest.signature_valid = false;
    mu_check(admit(&manifest, &environment) == PoisonContentUpdateAdmissionTampered);
    manifest = valid_manifest;
    manifest.signer_revoked = true;
    mu_check(admit(&manifest, &environment) == PoisonContentUpdateAdmissionRevokedSigner);
    manifest = valid_manifest;
    manifest.hardware_target = 6u;
    mu_check(admit(&manifest, &environment) == PoisonContentUpdateAdmissionWrongTarget);
    manifest = valid_manifest;
    manifest.minimum_api = 89u;
    mu_check(admit(&manifest, &environment) == PoisonContentUpdateAdmissionIncompatibleApi);
    manifest = valid_manifest;
    manifest.release_sequence = environment.highest_release_sequence;
    mu_check(admit(&manifest, &environment) == PoisonContentUpdateAdmissionDowngrade);
    manifest = valid_manifest;
    manifest.content_bytes = environment.available_storage_bytes + 1u;
    mu_check(admit(&manifest, &environment) == PoisonContentUpdateAdmissionInsufficientStorage);
    manifest = valid_manifest;
    manifest.rollback_available = false;
    mu_check(admit(&manifest, &environment) == PoisonContentUpdateAdmissionMissingRollback);
}

MU_TEST(poison_content_update_requires_complete_verified_payload) {
    PoisonContentUpdate update;
    mu_check(
        poison_content_update_admit(&update, &valid_manifest, &valid_environment) ==
        PoisonContentUpdateAdmissionOk);
    mu_check(poison_content_update_transition(&update, PoisonContentUpdateReceiving));
    mu_check(poison_content_update_receive(&update, 4096u));
    mu_check(!poison_content_update_transition(&update, PoisonContentUpdateStaged));
    mu_check(poison_content_update_receive(&update, 4096u));
    mu_check(!poison_content_update_receive(&update, 1u));
    mu_check(poison_content_update_transition(&update, PoisonContentUpdateStaged));
    mu_check(!poison_content_update_verify_payload(
        &update, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));
    mu_check(update.state == PoisonContentUpdateQuarantined);
}

MU_TEST(poison_content_update_requires_exact_confirmation_and_health) {
    PoisonContentUpdate update;
    mu_check(
        poison_content_update_admit(&update, &valid_manifest, &valid_environment) ==
        PoisonContentUpdateAdmissionOk);
    mu_check(poison_content_update_transition(&update, PoisonContentUpdateReceiving));
    mu_check(poison_content_update_receive(&update, valid_manifest.content_bytes));
    mu_check(poison_content_update_transition(&update, PoisonContentUpdateStaged));
    mu_check(poison_content_update_verify_payload(&update, valid_manifest.candidate_digest));
    mu_check(update.state == PoisonContentUpdateVerified);
    mu_check(poison_content_update_transition(&update, PoisonContentUpdateAwaitingConfirmation));
    mu_check(!poison_content_update_can_activate(&update));
    mu_check(!poison_content_update_confirm(&update, false));
    mu_check(poison_content_update_confirm(&update, true));
    mu_check(poison_content_update_can_activate(&update));
    mu_check(poison_content_update_transition(&update, PoisonContentUpdateActivating));
    mu_check(poison_content_update_report_health(&update, false));
    mu_check(update.state == PoisonContentUpdateRolledBack);
}

MU_TEST(poison_content_update_recovers_every_interrupted_activation_boundary) {
    const PoisonContentUpdateState interrupted_states[] = {
        PoisonContentUpdateReceiving,
        PoisonContentUpdateStaged,
        PoisonContentUpdateVerified,
        PoisonContentUpdateAwaitingConfirmation,
        PoisonContentUpdateActivating,
    };
    for(size_t index = 0; index < COUNT_OF(interrupted_states); ++index) {
        PoisonContentUpdate update;
        mu_check(
            poison_content_update_admit(&update, &valid_manifest, &valid_environment) ==
            PoisonContentUpdateAdmissionOk);
        update.state = interrupted_states[index];
        mu_check(poison_content_update_recover(&update, true));
        mu_check(update.state == PoisonContentUpdateRolledBack);
    }
}

MU_TEST(poison_content_update_cancel_restores_previous_verified_state) {
    PoisonContentUpdate update;
    mu_check(
        poison_content_update_admit(&update, &valid_manifest, &valid_environment) ==
        PoisonContentUpdateAdmissionOk);
    mu_check(poison_content_update_transition(&update, PoisonContentUpdateReceiving));
    mu_check(poison_content_update_cancel(&update));
    mu_check(update.state == PoisonContentUpdateRolledBack);
    mu_check(!poison_content_update_cancel(&update));
}

MU_TEST(poison_content_update_requires_verified_activation_order) {
    PoisonContentUpdate update;
    mu_check(poison_content_update_begin(
        &update,
        "firmware-1",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd",
        1u));
    mu_check(!poison_content_update_can_activate(&update));
    mu_check(poison_content_update_transition(&update, PoisonContentUpdateReceiving));
    mu_check(poison_content_update_transition(&update, PoisonContentUpdateStaged));
    mu_check(poison_content_update_transition(&update, PoisonContentUpdateVerified));
    mu_check(poison_content_update_transition(&update, PoisonContentUpdateAwaitingConfirmation));
    mu_check(poison_content_update_can_activate(&update));
    mu_check(poison_content_update_transition(&update, PoisonContentUpdateActivating));
    mu_check(poison_content_update_rollback(&update));
    mu_check(!poison_content_update_transition(&update, PoisonContentUpdateHealthy));
}

MU_TEST(poison_content_update_rejects_invalid_manifest_inputs) {
    PoisonContentUpdate update;
    mu_check(poison_content_update_begin(
        &update,
        "org.poison.app-1",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd",
        1u));
    mu_check(!poison_content_update_begin(&update, "../bad", "0", "0", 1u));
    mu_check(!poison_content_update_begin(
        &update,
        ".hidden",
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd",
        1u));
    mu_check(!poison_content_update_begin(&update, "firmware-1", "0", "0", 0u));
}

MU_TEST(poison_content_update_rpc_executes_bounded_operation_sequence) {
    RpcPoisonContentUpdate engine;
    bool activation_called = false;
    bool verification_called = false;
    PB_Poison_ContentUpdateRequest request = PB_Poison_ContentUpdateRequest_init_zero;
    PB_Poison_ContentUpdateStatus status = PB_Poison_ContentUpdateStatus_init_zero;
    RpcPoisonContentUpdateRequestContext request_context = {
        .session_id = 42u,
        .role = PoisonRoleOwner,
        .policy_version = 1u,
        .now_ms = 1000u,
        .physical_confirmed = false,
    };
    const char* lkg_path = "/ext/update/.poison-unit-lkg/update.fuf";
    Storage* storage = furi_record_open(RECORD_STORAGE);
    (void)storage_common_mkdir(storage, "/ext/update");
    (void)storage_common_mkdir(storage, "/ext/update/.poison-unit-lkg");
    File* lkg = storage_file_alloc(storage);
    mu_check(storage_file_open(lkg, lkg_path, FSAM_WRITE, FSOM_CREATE_ALWAYS));
    mu_check(storage_file_write(lkg, "lkg", 3u) == 3u);
    mu_check(storage_file_close(lkg));
    storage_file_free(lkg);
    furi_record_close(RECORD_STORAGE);
    rpc_poison_content_update_init(&engine);
    mu_check(rpc_poison_content_update_set_last_known_good(&engine, lkg_path));
    rpc_poison_content_update_set_environment(&engine, 7u, 88u, 1u, 16384u);
    rpc_poison_content_update_set_activation_callback(
        &engine, poison_content_update_test_activate, &activation_called);
    rpc_poison_content_update_set_verification_callback(
        &engine, poison_content_update_test_verify, &verification_called);

    request.operation = PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_IMPORT;
    strcpy(request.update_id, valid_manifest.update_id);
    request.content_type = PB_Poison_ContentUpdateType_CONTENT_UPDATE_TYPE_FIRMWARE;
    strcpy(request.candidate_digest, valid_manifest.candidate_digest);
    strcpy(request.previous_digest, valid_manifest.previous_digest);
    strcpy(request.manifest_path, "/ext/update/poison/update.poison");
    request.release_sequence = valid_manifest.release_sequence;
    request.content_bytes = valid_manifest.content_bytes;
    mu_check(rpc_poison_content_update_process(&engine, &request, &request_context, &status));
    mu_check(status.state == PB_Poison_ContentUpdateState_CONTENT_UPDATE_STATE_DISCOVERED);

    request = (PB_Poison_ContentUpdateRequest)PB_Poison_ContentUpdateRequest_init_zero;
    request.operation = PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_STAGE;
    strcpy(request.update_id, valid_manifest.update_id);
    request.received_bytes = valid_manifest.content_bytes;
    mu_check(rpc_poison_content_update_process(&engine, &request, &request_context, &status));
    mu_check(status.state == PB_Poison_ContentUpdateState_CONTENT_UPDATE_STATE_STAGED);

    request = (PB_Poison_ContentUpdateRequest)PB_Poison_ContentUpdateRequest_init_zero;
    request.operation = PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_VERIFY;
    strcpy(request.update_id, valid_manifest.update_id);
    strcpy(request.candidate_digest, valid_manifest.candidate_digest);
    mu_check(rpc_poison_content_update_process(&engine, &request, &request_context, &status));
    mu_check(verification_called);
    mu_check(
        status.state == PB_Poison_ContentUpdateState_CONTENT_UPDATE_STATE_AWAITING_CONFIRMATION);
    mu_check(status.confirmation_token.size == POISON_CONTENT_UPDATE_CONFIRMATION_TOKEN_BYTES);
    request = (PB_Poison_ContentUpdateRequest)PB_Poison_ContentUpdateRequest_init_zero;
    request.operation = PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_ACTIVATE;
    strcpy(request.update_id, valid_manifest.update_id);
    request.confirmation_token.size = status.confirmation_token.size;
    memcpy(
        request.confirmation_token.bytes,
        status.confirmation_token.bytes,
        status.confirmation_token.size);
    request.confirmation_token.bytes[0] ^= 1u;
    request_context.physical_confirmed = true;
    mu_check(!rpc_poison_content_update_process(&engine, &request, &request_context, &status));

    request.confirmation_token.bytes[0] ^= 1u;
    request_context.now_ms = 61000u;
    mu_check(!rpc_poison_content_update_confirmation_matches(&engine, &request, &request_context));
    request_context.now_ms = 1001u;
    request_context.physical_confirmed = false;
    mu_check(!rpc_poison_content_update_process(&engine, &request, &request_context, &status));
    request_context.physical_confirmed = true;
    mu_check(rpc_poison_content_update_process(&engine, &request, &request_context, &status));
    mu_check(activation_called);
    mu_check(status.state == PB_Poison_ContentUpdateState_CONTENT_UPDATE_STATE_ACTIVATING);
    strcpy(engine.activation_manifest_path, lkg_path);

    request = (PB_Poison_ContentUpdateRequest)PB_Poison_ContentUpdateRequest_init_zero;
    request.operation = PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_HEALTH;
    strcpy(request.update_id, valid_manifest.update_id);
    request.healthy = true;
    request_context.physical_confirmed = false;
    mu_check(!rpc_poison_content_update_process(&engine, &request, &request_context, &status));
    mu_check(engine.update.state == PoisonContentUpdateActivating);
    mu_check(strcmp(engine.accepted_digest, valid_manifest.previous_digest) == 0);

    request.healthy = false;
    mu_check(rpc_poison_content_update_process(&engine, &request, &request_context, &status));
    mu_check(status.state == PB_Poison_ContentUpdateState_CONTENT_UPDATE_STATE_ACTIVATING);
    mu_check(strcmp(status.result, "pending-health") == 0);

    mu_check(rpc_poison_content_update_promote_last_known_good(&engine, lkg_path));
    mu_check(poison_content_update_report_health(&engine.update, true));
    strcpy(engine.accepted_digest, engine.update.candidate_digest);
    engine.highest_release_sequence = engine.update.sequence;
    mu_check(engine.update.state == PoisonContentUpdateHealthy);

    const char* state_path = EXT_PATH(".tmp/poison-content-update-state.bin");
    mu_check(rpc_poison_content_update_save(&engine, state_path));
    RpcPoisonContentUpdate restored;
    rpc_poison_content_update_init(&restored);
    mu_check(rpc_poison_content_update_load(&restored, state_path));
    mu_check(restored.active);
    mu_check(restored.update.state == PoisonContentUpdateHealthy);
    mu_check(strcmp(restored.update.update_id, valid_manifest.update_id) == 0);
    mu_check(restored.highest_release_sequence == valid_manifest.release_sequence);

    storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    mu_check(storage_file_open(file, state_path, FSAM_READ_WRITE, FSOM_OPEN_EXISTING));
    uint8_t first = 0u;
    mu_check(storage_file_read(file, &first, 1u) == 1u);
    first ^= 0x80u;
    mu_check(storage_file_seek(file, 0u, true));
    mu_check(storage_file_write(file, &first, 1u) == 1u);
    mu_check(storage_file_close(file));
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    rpc_poison_content_update_init(&restored);
    mu_check(!rpc_poison_content_update_load(&restored, state_path));
    storage = furi_record_open(RECORD_STORAGE);
    mu_check(storage_simply_remove(storage, state_path));
    mu_check(storage_simply_remove(storage, lkg_path));
    mu_check(storage_simply_remove(storage, "/ext/update/.poison-unit-lkg"));
    furi_record_close(RECORD_STORAGE);
}

MU_TEST(poison_content_update_rpc_rejects_firmware_without_real_rollback_artifact) {
    RpcPoisonContentUpdate engine;
    bool verification_called = false;
    PB_Poison_ContentUpdateRequest request = PB_Poison_ContentUpdateRequest_init_zero;
    PB_Poison_ContentUpdateStatus status = PB_Poison_ContentUpdateStatus_init_zero;
    RpcPoisonContentUpdateRequestContext request_context = {
        .session_id = 42u,
        .role = PoisonRoleOwner,
        .policy_version = 1u,
        .now_ms = 1000u,
    };
    rpc_poison_content_update_init(&engine);
    rpc_poison_content_update_set_environment(&engine, 7u, 88u, 1u, 16384u);
    rpc_poison_content_update_set_verification_callback(
        &engine, poison_content_update_test_verify, &verification_called);

    request.operation = PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_IMPORT;
    strcpy(request.update_id, valid_manifest.update_id);
    request.content_type = PB_Poison_ContentUpdateType_CONTENT_UPDATE_TYPE_FIRMWARE;
    strcpy(request.candidate_digest, valid_manifest.candidate_digest);
    strcpy(request.previous_digest, valid_manifest.previous_digest);
    strcpy(request.manifest_path, "/ext/update/poison/update.poison");
    request.release_sequence = valid_manifest.release_sequence;
    request.content_bytes = valid_manifest.content_bytes;

    mu_check(!rpc_poison_content_update_process(&engine, &request, &request_context, &status));
    mu_check(verification_called);
    mu_check(!engine.active);
}

MU_TEST(poison_content_update_rpc_rollback_prepares_previous_firmware) {
    static const char current_path[] = "/ext/update/current/update.fuf";
    static const char previous_path[] = "/ext/update/previous/update.fuf";
    RpcPoisonContentUpdate engine;
    rpc_poison_content_update_init(&engine);
    mu_check(
        poison_content_update_admit(&engine.update, &valid_manifest, &valid_environment) ==
        PoisonContentUpdateAdmissionOk);
    engine.active = true;
    engine.update.state = PoisonContentUpdateHealthy;
    strcpy(engine.accepted_digest, valid_manifest.candidate_digest);
    strcpy(engine.last_known_good_manifest_path, current_path);
    strcpy(engine.previous_known_good_manifest_path, previous_path);
    PoisonContentUpdateRollbackFixture rollback = {
        .expected_path = previous_path,
    };
    rpc_poison_content_update_set_rollback_callback(
        &engine, poison_content_update_test_rollback, &rollback);
    PB_Poison_ContentUpdateRequest request = PB_Poison_ContentUpdateRequest_init_zero;
    request.operation = PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_ROLLBACK;
    strcpy(request.update_id, valid_manifest.update_id);
    PB_Poison_ContentUpdateStatus status = PB_Poison_ContentUpdateStatus_init_zero;

    mu_check(rpc_poison_content_update_process(&engine, &request, NULL, &status));
    mu_check(rollback.called);
    mu_check(engine.update.state == PoisonContentUpdateRolledBack);
    mu_check(strcmp(engine.last_known_good_manifest_path, previous_path) == 0);
    mu_check(strcmp(engine.previous_known_good_manifest_path, current_path) == 0);
    mu_check(strcmp(engine.accepted_digest, valid_manifest.previous_digest) == 0);
}

MU_TEST(poison_content_update_boot_health_marker_is_bound_and_tamper_evident) {
    const char* pending_path = EXT_PATH(".tmp/poison-content-update.pending");
    const char* complete_path = EXT_PATH(".tmp/poison-content-update.complete");
    Storage* storage = furi_record_open(RECORD_STORAGE);
    (void)storage_common_mkdir(storage, EXT_PATH(".tmp"));
    (void)storage_simply_remove(storage, pending_path);
    (void)storage_simply_remove(storage, complete_path);
    furi_record_close(RECORD_STORAGE);

    PoisonContentUpdate update;
    mu_check(
        poison_content_update_admit(&update, &valid_manifest, &valid_environment) ==
        PoisonContentUpdateAdmissionOk);
    mu_check(poison_content_update_transition(&update, PoisonContentUpdateReceiving));
    mu_check(poison_content_update_receive(&update, valid_manifest.content_bytes));
    mu_check(poison_content_update_transition(&update, PoisonContentUpdateStaged));
    mu_check(poison_content_update_verify_payload(&update, valid_manifest.candidate_digest));
    mu_check(poison_content_update_transition(&update, PoisonContentUpdateAwaitingConfirmation));
    mu_check(poison_content_update_confirm(&update, true));
    mu_check(poison_content_update_health_arm_at(&update, pending_path));
    mu_check(poison_content_update_health_mark_complete_at(pending_path, complete_path));
    mu_check(poison_content_update_transition(&update, PoisonContentUpdateActivating));
    mu_check(poison_content_update_health_completed_at(&update, complete_path));

    storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    mu_check(storage_file_open(file, complete_path, FSAM_READ_WRITE, FSOM_OPEN_EXISTING));
    uint8_t first = 0u;
    mu_check(storage_file_read(file, &first, 1u) == 1u);
    first ^= 0x80u;
    mu_check(storage_file_seek(file, 0u, true));
    mu_check(storage_file_write(file, &first, 1u) == 1u);
    mu_check(storage_file_close(file));
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    mu_check(!poison_content_update_health_completed_at(&update, complete_path));

    poison_content_update_health_clear_at(pending_path, complete_path);
    mu_check(poison_content_update_health_mark_complete_at(pending_path, complete_path));
    storage = furi_record_open(RECORD_STORAGE);
    mu_check(storage_common_stat(storage, complete_path, NULL) == FSE_NOT_EXIST);
    furi_record_close(RECORD_STORAGE);
}

MU_TEST_SUITE(poison_content_update_suite) {
    MU_RUN_TEST(poison_content_update_admits_exact_signed_contract);
    MU_RUN_TEST(poison_content_update_rejects_untrusted_or_incompatible_contracts);
    MU_RUN_TEST(poison_content_update_requires_complete_verified_payload);
    MU_RUN_TEST(poison_content_update_requires_exact_confirmation_and_health);
    MU_RUN_TEST(poison_content_update_recovers_every_interrupted_activation_boundary);
    MU_RUN_TEST(poison_content_update_cancel_restores_previous_verified_state);
    MU_RUN_TEST(poison_content_update_requires_verified_activation_order);
    MU_RUN_TEST(poison_content_update_rejects_invalid_manifest_inputs);
    MU_RUN_TEST(poison_content_update_rpc_executes_bounded_operation_sequence);
    MU_RUN_TEST(poison_content_update_rpc_rejects_firmware_without_real_rollback_artifact);
    MU_RUN_TEST(poison_content_update_rpc_rollback_prepares_previous_firmware);
    MU_RUN_TEST(poison_content_update_boot_health_marker_is_bound_and_tamper_evident);
}

void poison_content_update_run_tests(void) {
    MU_RUN_SUITE(poison_content_update_suite);
}
