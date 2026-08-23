#include "../test.h"
#include "../../../../services/poison_packages/poison_package_manager.h"
#include "../../../../services/poison_packages/poison_package_transaction.h"
#include "../../../../services/rpc/rpc_poison_packages.h"

#include <string.h>
#include <furi.h>
#include <storage/storage.h>

MU_TEST(poison_package_transaction_preserves_verified_rollback) {
    const PoisonContentUpdateManifest manifest = {
        .content_type = PoisonContentUpdateApplication,
        .update_id = "org.poison.app-2",
        .candidate_digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .previous_digest = "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd",
        .hardware_target = 7u,
        .minimum_api = 88u,
        .maximum_api = 89u,
        .release_sequence = 2u,
        .content_bytes = 4096u,
        .signature_valid = true,
        .rollback_available = true,
        .protected_target = true,
    };
    const PoisonContentUpdateEnvironment environment = {
        .hardware_target = 7u,
        .firmware_api = 88u,
        .highest_release_sequence = 1u,
        .available_storage_bytes = 8192u,
    };
    PoisonPackageTransaction transaction;
    mu_check(
        poison_package_transaction_begin(
            &transaction, &manifest, &environment, PoisonPackageActive, false) ==
        PoisonContentUpdateAdmissionOk);
    mu_check(poison_package_receive(&transaction, manifest.content_bytes));
    mu_check(transaction.state == PoisonPackageStaged);
    mu_check(poison_package_verify_payload(&transaction, manifest.candidate_digest));
    mu_check(transaction.state == PoisonPackageVerified);
    mu_check(poison_package_activate(&transaction, true));
    mu_check(transaction.state == PoisonPackageVerified);
    mu_check(transaction.content_update.state == PoisonContentUpdateActivating);
    mu_check(poison_package_report_health(&transaction, false));
    mu_check(transaction.state == PoisonPackageActive);
    mu_check(transaction.content_update.state == PoisonContentUpdateRolledBack);
}

MU_TEST(poison_package_transaction_allows_confirmation_retry) {
    const PoisonContentUpdateManifest manifest = {
        .content_type = PoisonContentUpdateApplication,
        .update_id = "org.poison.retry-2",
        .candidate_digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .previous_digest = "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd",
        .hardware_target = 7u,
        .minimum_api = 88u,
        .maximum_api = 89u,
        .release_sequence = 2u,
        .content_bytes = 4096u,
        .signature_valid = true,
        .rollback_available = true,
        .protected_target = true,
    };
    const PoisonContentUpdateEnvironment environment = {
        .hardware_target = 7u,
        .firmware_api = 88u,
        .highest_release_sequence = 1u,
        .available_storage_bytes = 8192u,
    };
    PoisonPackageTransaction transaction;
    mu_check(
        poison_package_transaction_begin(
            &transaction, &manifest, &environment, PoisonPackageActive, false) ==
        PoisonContentUpdateAdmissionOk);
    mu_check(poison_package_receive(&transaction, manifest.content_bytes));
    mu_check(poison_package_verify_payload(&transaction, manifest.candidate_digest));
    mu_check(!poison_package_activate(&transaction, false));
    mu_check(transaction.content_update.state == PoisonContentUpdateAwaitingConfirmation);
    mu_check(poison_package_activate(&transaction, true));
    mu_check(transaction.content_update.state == PoisonContentUpdateActivating);
}

MU_TEST(poison_package_transaction_protects_known_good_package) {
    PoisonPackageTransaction transaction = {
        .state = PoisonPackageActive, .protected_package = true};
    mu_check(!poison_package_transition(&transaction, PoisonPackageRemoved));
}

MU_TEST(poison_package_manager_drives_verified_lifecycle_and_rollback) {
    PoisonPackageManager manager;
    poison_package_manager_init(&manager);

    PoisonPackageImport package = {
        .content_type = "application",
        .package_id = "org.poison.tool",
        .version = "2.0.0",
        .previous_version = "1.0.0",
        .candidate_digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .previous_digest = "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd",
        .signing_key_id = "package-prod-1",
        .manifest_path = "/ext/apps/.staging/org.poison.tool/package.poison",
        .entrypoint = "app.fap",
        .capability_mask = 0x20u,
        .release_sequence = 2u,
        .content_bytes = 4096u,
        .hardware_target = 7u,
        .firmware_api = 88u,
        .highest_release_sequence = 1u,
        .available_storage_bytes = 8192u,
        .previous_state = PoisonPackageActive,
        .protected_package = false,
        .confirmation_required = true,
        .manifest_verified = true,
    };

    mu_check(poison_package_manager_import(&manager, &package));
    mu_check(poison_package_manager_receive(&manager, package.package_id, 4096u));
    mu_check(poison_package_manager_verify(
        &manager, package.package_id, package.candidate_digest, true));
    mu_check(!poison_package_manager_activate(&manager, package.package_id, false));
    mu_check(poison_package_manager_activate(&manager, package.package_id, true));
    mu_check(poison_package_manager_report_health(&manager, package.package_id, false));

    const PoisonPackageRecord* record = poison_package_manager_find(&manager, package.package_id);
    mu_check(record);
    mu_check(record->transaction.state == PoisonPackageActive);
    mu_check(strcmp(record->version, "1.0.0") == 0);
    mu_check(strcmp(record->digest, package.previous_digest) == 0);
}

MU_TEST(poison_package_manager_rejects_duplicate_and_traversal_imports) {
    PoisonPackageManager manager;
    poison_package_manager_init(&manager);
    PoisonPackageImport package = {
        .content_type = "application",
        .package_id = "org.poison.safe",
        .version = "1.0.0",
        .candidate_digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .signing_key_id = "package-prod-1",
        .manifest_path = "/ext/apps/.staging/org.poison.safe/package.poison",
        .entrypoint = "app.fap",
        .release_sequence = 1u,
        .content_bytes = 1u,
        .hardware_target = 7u,
        .firmware_api = 88u,
        .available_storage_bytes = 1u,
        .previous_state = PoisonPackageRemoved,
        .manifest_verified = true,
    };
    mu_check(poison_package_manager_import(&manager, &package));
    mu_check(!poison_package_manager_import(&manager, &package));
    package.package_id = "org.poison.escape";
    package.manifest_path = "/ext/apps/../config/escape";
    mu_check(!poison_package_manager_import(&manager, &package));
}

MU_TEST(poison_package_manager_updates_only_from_exact_known_good_version) {
    PoisonPackageManager manager;
    poison_package_manager_init(&manager);
    PoisonPackageImport initial = {
        .content_type = "application",
        .package_id = "org.poison.update",
        .version = "1.0.0",
        .candidate_digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .signing_key_id = "package-prod-1",
        .manifest_path = "/ext/apps/.staging/org.poison.update/one.poison",
        .entrypoint = "app.fap",
        .release_sequence = 1u,
        .content_bytes = 64u,
        .hardware_target = 7u,
        .firmware_api = 88u,
        .available_storage_bytes = 4096u,
        .previous_state = PoisonPackageRemoved,
        .manifest_verified = true,
    };
    mu_check(poison_package_manager_import(&manager, &initial));
    mu_check(poison_package_manager_receive(&manager, initial.package_id, 64u));
    mu_check(poison_package_manager_verify(
        &manager, initial.package_id, initial.candidate_digest, true));
    mu_check(poison_package_manager_activate(&manager, initial.package_id, true));
    mu_check(poison_package_manager_report_health(&manager, initial.package_id, true));

    PoisonPackageImport update = initial;
    update.version = "2.0.0";
    update.previous_version = "wrong";
    update.previous_digest = initial.candidate_digest;
    update.candidate_digest = "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";
    update.manifest_path = "/ext/apps/.staging/org.poison.update/two.poison";
    update.release_sequence = 2u;
    update.previous_state = PoisonPackageActive;
    mu_check(!poison_package_manager_import(&manager, &update));
    update.previous_version = "1.0.0";
    mu_check(poison_package_manager_import(&manager, &update));
    const PoisonPackageRecord* record = poison_package_manager_find(&manager, initial.package_id);
    mu_check(record);
    mu_check(strcmp(record->version, "2.0.0") == 0);
    mu_check(strcmp(record->previous_version, "1.0.0") == 0);
    mu_check(strcmp(record->previous_digest, initial.candidate_digest) == 0);
}

MU_TEST(poison_package_manager_retains_and_swaps_known_good_version) {
    PoisonPackageManager manager;
    poison_package_manager_init(&manager);
    PoisonPackageImport initial = {
        .content_type = "application",
        .package_id = "org.poison.rollback",
        .version = "1.0.0",
        .candidate_digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .signing_key_id = "package-prod-1",
        .manifest_path = "/ext/apps/.staging/org.poison.rollback/one.poison",
        .entrypoint = "app.fap",
        .release_sequence = 1u,
        .content_bytes = 64u,
        .hardware_target = 7u,
        .firmware_api = 88u,
        .available_storage_bytes = 4096u,
        .previous_state = PoisonPackageRemoved,
        .manifest_verified = true,
    };
    mu_check(poison_package_manager_import(&manager, &initial));
    mu_check(poison_package_manager_receive(&manager, initial.package_id, 64u));
    mu_check(poison_package_manager_verify(
        &manager, initial.package_id, initial.candidate_digest, true));
    mu_check(poison_package_manager_activate(&manager, initial.package_id, true));
    mu_check(poison_package_manager_report_health(&manager, initial.package_id, true));

    PoisonPackageImport update = initial;
    update.version = "2.0.0";
    update.previous_version = "1.0.0";
    update.previous_digest = initial.candidate_digest;
    update.candidate_digest = "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";
    update.manifest_path = "/ext/apps/.staging/org.poison.rollback/two.poison";
    update.release_sequence = 2u;
    update.previous_state = PoisonPackageActive;
    mu_check(poison_package_manager_import(&manager, &update));
    mu_check(poison_package_manager_receive(&manager, update.package_id, 64u));
    mu_check(
        poison_package_manager_verify(&manager, update.package_id, update.candidate_digest, true));
    mu_check(poison_package_manager_activate(&manager, update.package_id, true));
    mu_check(poison_package_manager_report_health(&manager, update.package_id, true));
    mu_check(poison_package_manager_rollback(&manager, update.package_id, true));

    const PoisonPackageRecord* record = poison_package_manager_find(&manager, update.package_id);
    mu_check(record);
    mu_check(strcmp(record->version, "1.0.0") == 0);
    mu_check(strcmp(record->digest, initial.candidate_digest) == 0);
    mu_check(strcmp(record->previous_version, "2.0.0") == 0);
    mu_check(strcmp(record->previous_digest, update.candidate_digest) == 0);
    mu_check(record->transaction.state == PoisonPackageActive);
    mu_check(record->transaction.content_update.state == PoisonContentUpdateRolledBack);
}

MU_TEST(poison_package_manager_persists_and_rejects_corrupt_inventory) {
    PoisonPackageManager manager;
    poison_package_manager_init(&manager);
    PoisonPackageImport package = {
        .content_type = "application",
        .package_id = "org.poison.persist",
        .version = "1.0.0",
        .candidate_digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .signing_key_id = "package-prod-1",
        .manifest_path = "/ext/apps/.staging/org.poison.persist/package.poison",
        .entrypoint = "app.fap",
        .release_sequence = 1u,
        .content_bytes = 64u,
        .hardware_target = 7u,
        .firmware_api = 88u,
        .available_storage_bytes = 4096u,
        .previous_state = PoisonPackageRemoved,
        .manifest_verified = true,
    };
    mu_check(poison_package_manager_import(&manager, &package));
    mu_check(poison_package_manager_receive(&manager, package.package_id, 64u));
    mu_check(poison_package_manager_verify(
        &manager, package.package_id, package.candidate_digest, true));
    mu_check(poison_package_manager_activate(&manager, package.package_id, true));
    mu_check(poison_package_manager_report_health(&manager, package.package_id, true));

    const char* path = EXT_PATH(".tmp/poison-package-state.bin");
    mu_check(poison_package_manager_save(&manager, path));
    PoisonPackageManager restored;
    poison_package_manager_init(&restored);
    mu_check(poison_package_manager_load(&restored, path));
    const PoisonPackageRecord* record = poison_package_manager_find(&restored, package.package_id);
    mu_check(record);
    mu_check(strcmp(record->version, package.version) == 0);
    mu_check(record->transaction.state == PoisonPackageActive);
    mu_check(restored.generation == manager.generation);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    mu_check(storage_file_open(file, path, FSAM_READ_WRITE, FSOM_OPEN_EXISTING));
    uint8_t first = 0u;
    mu_check(storage_file_read(file, &first, 1u) == 1u);
    first ^= 0x80u;
    mu_check(storage_file_seek(file, 0u, true));
    mu_check(storage_file_write(file, &first, 1u) == 1u);
    mu_check(storage_file_close(file));
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    poison_package_manager_init(&restored);
    mu_check(!poison_package_manager_load(&restored, path));
    storage = furi_record_open(RECORD_STORAGE);
    mu_check(storage_simply_remove(storage, path));
    furi_record_close(RECORD_STORAGE);
}

MU_TEST(poison_package_manager_preserves_type_and_requires_active_healthy_asset) {
    PoisonPackageManager manager;
    poison_package_manager_init(&manager);
    PoisonPackageImport package = {
        .content_type = "theme",
        .package_id = "org.poison.theme.night",
        .version = "1.0.0",
        .candidate_digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .signing_key_id = "package-prod-1",
        .manifest_path = "/ext/apps/.staging/org.poison.theme.night/package.poison",
        .entrypoint = "theme.json",
        .release_sequence = 1u,
        .content_bytes = 64u,
        .hardware_target = 7u,
        .firmware_api = 88u,
        .available_storage_bytes = 4096u,
        .previous_state = PoisonPackageRemoved,
        .manifest_verified = true,
    };

    mu_check(poison_package_manager_import(&manager, &package));
    const PoisonPackageRecord* record = poison_package_manager_find(&manager, package.package_id);
    mu_check(record);
    mu_check(strcmp(record->content_type, "theme") == 0);
    mu_check(!poison_package_manager_active_content(&manager, package.package_id, "theme"));
    mu_check(poison_package_manager_receive(&manager, package.package_id, package.content_bytes));
    mu_check(poison_package_manager_verify(
        &manager, package.package_id, package.candidate_digest, true));
    mu_check(poison_package_manager_activate(&manager, package.package_id, true));
    mu_check(!poison_package_manager_active_content(&manager, package.package_id, "theme"));
    mu_check(poison_package_manager_report_health(&manager, package.package_id, true));
    mu_check(poison_package_manager_active_content(&manager, package.package_id, "theme"));
    mu_check(!poison_package_manager_active_content(&manager, package.package_id, "icon"));

    package.package_id = "org.poison.bad-firmware";
    package.content_type = "firmware";
    mu_check(!poison_package_manager_import(&manager, &package));
    package.content_type = "unknown";
    mu_check(!poison_package_manager_import(&manager, &package));
}

MU_TEST(poison_package_manager_supports_disable_quarantine_remove_and_reinstall) {
    PoisonPackageManager manager;
    poison_package_manager_init(&manager);
    PoisonPackageImport package = {
        .content_type = "application",
        .package_id = "org.poison.lifecycle",
        .version = "1.0.0",
        .candidate_digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .signing_key_id = "package-prod-1",
        .manifest_path = "/ext/apps/.staging/org.poison.lifecycle/package.poison",
        .entrypoint = "app.fap",
        .release_sequence = 1u,
        .content_bytes = 64u,
        .hardware_target = 7u,
        .firmware_api = 88u,
        .available_storage_bytes = 4096u,
        .previous_state = PoisonPackageRemoved,
        .confirmation_required = true,
        .manifest_verified = true,
    };
    mu_check(poison_package_manager_import(&manager, &package));
    mu_check(poison_package_manager_receive(&manager, package.package_id, 64u));
    mu_check(poison_package_manager_verify(
        &manager, package.package_id, package.candidate_digest, true));
    mu_check(poison_package_manager_activate(&manager, package.package_id, true));
    mu_check(poison_package_manager_report_health(&manager, package.package_id, true));
    mu_check(poison_package_manager_set_enabled(&manager, package.package_id, false, false));
    mu_check(!poison_package_manager_set_enabled(&manager, package.package_id, true, false));
    mu_check(poison_package_manager_set_enabled(&manager, package.package_id, true, true));
    mu_check(poison_package_manager_quarantine(&manager, package.package_id));
    mu_check(poison_package_manager_remove(&manager, package.package_id, true));
    mu_check(
        poison_package_manager_find(&manager, package.package_id)->transaction.state ==
        PoisonPackageRemoved);

    package.version = "2.0.0";
    package.candidate_digest = "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";
    package.manifest_path = "/ext/apps/.staging/org.poison.lifecycle/reinstall.poison";
    package.release_sequence = 2u;
    mu_check(poison_package_manager_import(&manager, &package));
    const PoisonPackageRecord* reinstalled =
        poison_package_manager_find(&manager, package.package_id);
    mu_check(reinstalled);
    mu_check(strcmp(reinstalled->version, "2.0.0") == 0);
    mu_check(reinstalled->transaction.state == PoisonPackageRemoved);

    for(size_t index = 1u; index < POISON_PACKAGE_MAX_RECORDS; ++index) {
        char id[32u];
        char path[96u];
        snprintf(id, sizeof(id), "org.poison.slot-%u", (unsigned int)index);
        snprintf(
            path,
            sizeof(path),
            "/ext/apps/.staging/org.poison.slot-%u/package.poison",
            (unsigned int)index);
        package.package_id = id;
        package.manifest_path = path;
        package.version = "1.0.0";
        package.candidate_digest =
            index % 2u ? "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" :
                         "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";
        package.release_sequence = (uint32_t)index + 2u;
        mu_check(poison_package_manager_import(&manager, &package));
    }
    mu_check(poison_package_manager_count(&manager) == POISON_PACKAGE_MAX_RECORDS);
    PoisonPackageRecord* reusable = &manager.records[POISON_PACKAGE_MAX_RECORDS - 1u];
    reusable->transaction.state = PoisonPackageRemoved;
    package.package_id = "org.poison.reused-slot";
    package.manifest_path = "/ext/apps/.staging/org.poison.reused-slot/package.poison";
    package.release_sequence++;
    mu_check(poison_package_manager_import(&manager, &package));
    mu_check(poison_package_manager_find(&manager, "org.poison.reused-slot"));
}

MU_TEST(poison_package_rpc_engine_requires_verified_manifest_token_and_device_approval) {
    RpcPoisonPackages packages;
    PoisonPackageManager manager;
    poison_package_manager_init(&manager);
    rpc_poison_packages_init(&packages, &manager);
    rpc_poison_packages_set_environment(&packages, 7u, 88u, 46u, 4096u);
    PB_Poison_PackageOperationRequest request = PB_Poison_PackageOperationRequest_init_zero;
    PB_Poison_PackageOperationStatus status = PB_Poison_PackageOperationStatus_init_zero;
    RpcPoisonPackagesRequestContext request_context = {
        .session_id = 42u,
        .role = PoisonRoleOwner,
        .policy_version = 1u,
        .now_ms = 1000u,
    };

    request.operation = PB_Poison_PackageOperation_PACKAGE_OPERATION_IMPORT;
    strcpy(request.package_id, "org.poison.rpc");
    strcpy(request.version, "1.0.0");
    strcpy(request.manifest_path, "/ext/apps/.staging/org.poison.rpc/package.poison");
    strcpy(
        request.candidate_digest,
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    strcpy(request.signing_key_id, "package-prod-1");
    request.release_sequence = 1u;
    request.content_bytes = 64u;
    request.previous_state = PB_Poison_PackageLifecycleState_PACKAGE_LIFECYCLE_STATE_REMOVED;
    request.confirmation_required = true;

    PoisonPackageVerifiedArchive verified = {0};
    strcpy(verified.content_type, "application");
    strcpy(verified.package_id, request.package_id);
    strcpy(verified.version, request.version);
    strcpy(verified.entrypoint, "app.fap");
    strcpy(verified.signing_key_id, request.signing_key_id);
    strcpy(verified.archive_sha256, request.candidate_digest);
    verified.release_sequence = request.release_sequence;
    verified.archive_bytes = request.content_bytes;

    mu_check(!rpc_poison_packages_process(&packages, &request, NULL, &request_context, &status));
    mu_check(
        rpc_poison_packages_process(&packages, &request, &verified, &request_context, &status));

    request.operation = PB_Poison_PackageOperation_PACKAGE_OPERATION_STAGE;
    request.received_bytes = 64u;
    mu_check(rpc_poison_packages_process(&packages, &request, NULL, &request_context, &status));

    request.operation = PB_Poison_PackageOperation_PACKAGE_OPERATION_VERIFY;
    mu_check(
        rpc_poison_packages_process(&packages, &request, &verified, &request_context, &status));
    mu_check(status.confirmation_token.size == POISON_PACKAGE_CONFIRMATION_TOKEN_BYTES);
    memcpy(
        request.confirmation_token.bytes,
        status.confirmation_token.bytes,
        status.confirmation_token.size);
    request.confirmation_token.size = status.confirmation_token.size;

    request.operation = PB_Poison_PackageOperation_PACKAGE_OPERATION_ACTIVATE;
    request.confirmation_token.bytes[0] ^= 0x80u;
    request_context.physical_confirmed = true;
    request_context.now_ms = 1001u;
    mu_check(!rpc_poison_packages_process(&packages, &request, NULL, &request_context, &status));
    memcpy(
        request.confirmation_token.bytes,
        status.confirmation_token.bytes,
        status.confirmation_token.size);
    request.confirmation_token.size = status.confirmation_token.size;
    request.capability_mask ^= 1u;
    mu_check(!rpc_poison_packages_process(&packages, &request, NULL, &request_context, &status));
    request.capability_mask ^= 1u;
    mu_check(!rpc_poison_packages_process(&packages, &request, NULL, &request_context, &status));
    memcpy(
        request.confirmation_token.bytes,
        status.confirmation_token.bytes,
        status.confirmation_token.size);
    request.confirmation_token.size = status.confirmation_token.size;
    request_context.physical_confirmed = false;
    mu_check(!rpc_poison_packages_process(&packages, &request, NULL, &request_context, &status));
    request_context.now_ms = 61001u;
    request_context.physical_confirmed = true;
    mu_check(!rpc_poison_packages_process(&packages, &request, NULL, &request_context, &status));
    memcpy(
        request.confirmation_token.bytes,
        status.confirmation_token.bytes,
        status.confirmation_token.size);
    request.confirmation_token.size = status.confirmation_token.size;
    request_context.now_ms = 61002u;
    mu_check(rpc_poison_packages_process(&packages, &request, NULL, &request_context, &status));
    mu_check(!rpc_poison_packages_process(&packages, &request, NULL, &request_context, &status));
}

MU_TEST_SUITE(poison_package_transaction_suite) {
    MU_RUN_TEST(poison_package_transaction_preserves_verified_rollback);
    MU_RUN_TEST(poison_package_transaction_allows_confirmation_retry);
    MU_RUN_TEST(poison_package_transaction_protects_known_good_package);
    MU_RUN_TEST(poison_package_manager_drives_verified_lifecycle_and_rollback);
    MU_RUN_TEST(poison_package_manager_rejects_duplicate_and_traversal_imports);
    MU_RUN_TEST(poison_package_manager_updates_only_from_exact_known_good_version);
    MU_RUN_TEST(poison_package_manager_retains_and_swaps_known_good_version);
    MU_RUN_TEST(poison_package_manager_persists_and_rejects_corrupt_inventory);
    MU_RUN_TEST(poison_package_manager_preserves_type_and_requires_active_healthy_asset);
    MU_RUN_TEST(poison_package_manager_supports_disable_quarantine_remove_and_reinstall);
    MU_RUN_TEST(poison_package_rpc_engine_requires_verified_manifest_token_and_device_approval);
}
void poison_package_transaction_run_tests(void) {
    MU_RUN_SUITE(poison_package_transaction_suite);
}
