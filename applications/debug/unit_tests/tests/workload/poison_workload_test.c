#include "../../../../services/poison_workload/poison_workload.h"
#include "../../../../services/poison_workload/poison_workload_i.h"
#include "../../../../services/poison_workload/poison_js_bundle.h"
#include "../../../../services/poison_workload/poison_js_bundle_i.h"
#include "../../../../services/poison_workload/poison_workload_native_adapter.h"
#include "../../../../services/poison_evidence/poison_evidence_i.h"
#include "../../../../services/rpc/rpc_poison_workload.h"
#include "../../../../system/js_app/js_capabilities.h"

#include "../test.h"

#include <stdio.h>
#include <string.h>

#include <furi.h>
#include <mbedtls/sha256.h>
#include <storage/storage.h>

static const char* digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static PoisonWorkloadLimits test_limits(void) {
    return (PoisonWorkloadLimits){
        .heap_bytes = 1024,
        .source_bytes = 1024,
        .modules = 4,
        .parser_depth = 8,
        .stack_depth = 8,
        .fuel = 100,
        .callbacks = 4,
        .timers = 4,
        .open_handles = 4,
        .logs = 4,
        .artifacts = 2,
        .wall_ms = 1000,
        .artifact_bytes = 4096,
    };
}

MU_TEST(poison_workload_lifecycle_and_accounting) {
    PoisonWorkload workload;
    PoisonWorkloadLimits limits = test_limits();
    mu_check(poison_workload_init(&workload, "run-1", digest, digest, &limits));
    mu_check(poison_workload_start(&workload));
    mu_check(poison_workload_append_console(&workload, PoisonWorkloadConsoleStdout, "ready"));
    PoisonWorkloadUsage usage = {.fuel = 10, .wall_ms = 5};
    mu_check(poison_workload_account(&workload, &usage, PoisonWorkloadTerminalFuelLimit));
    mu_check(workload.usage.heap_bytes == 0u);
    PoisonWorkloadUsage heap = {.heap_bytes = 17u};
    mu_check(poison_workload_account(&workload, &heap, PoisonWorkloadTerminalHeapLimit));
    mu_check(workload.usage.heap_bytes == 17u);
    mu_check(poison_workload_finish(&workload, true));
    mu_check(workload.state == PoisonWorkloadCompleted);
    mu_check(workload.console[0].sequence == 1u);
}

MU_TEST(poison_workload_limit_and_authorization) {
    PoisonWorkload workload;
    PoisonWorkloadLimits limits = test_limits();
    PoisonWorkloadAuthorization authorization = {.create = true, .run = false, .inspect = true};
    mu_check(poison_workload_authorize(&authorization, PoisonWorkloadOperationCreate));
    mu_check(!poison_workload_authorize(&authorization, PoisonWorkloadOperationRun));
    mu_check(poison_workload_init(&workload, "run-2", digest, digest, &limits));
    mu_check(poison_workload_start(&workload));
    PoisonWorkloadUsage usage = {.fuel = 101};
    mu_check(poison_workload_account(&workload, &usage, PoisonWorkloadTerminalFuelLimit));
    mu_check(workload.state == PoisonWorkloadFailed);
    mu_check(workload.terminal_reason == PoisonWorkloadTerminalFuelLimit);
    mu_check(!poison_workload_append_console(&workload, PoisonWorkloadConsoleStdout, "late"));
}

MU_TEST(poison_workload_cancel_and_artifact) {
    PoisonWorkload workload;
    PoisonWorkloadLimits limits = test_limits();
    mu_check(poison_workload_init(&workload, "run-3", digest, digest, &limits));
    mu_check(poison_workload_start(&workload));
    mu_check(poison_workload_finalize_artifact(&workload, true));
    mu_check(poison_workload_request_cancel(&workload));
    mu_check(poison_workload_force_terminate(&workload, PoisonWorkloadTerminalCancelled));
    mu_check(workload.state == PoisonWorkloadCancelled);
    mu_check(!poison_workload_finalize_artifact(&workload, true));
}

MU_TEST(poison_workload_log_limit_is_terminal) {
    PoisonWorkload workload;
    PoisonWorkloadLimits limits = test_limits();
    limits.logs = 3;
    mu_check(poison_workload_init(&workload, "run-log", digest, digest, &limits));
    mu_check(poison_workload_start(&workload));
    mu_check(poison_workload_append_console(&workload, PoisonWorkloadConsoleLog, "one"));
    mu_check(!poison_workload_append_console(&workload, PoisonWorkloadConsoleLog, "two"));
    mu_check(workload.terminal_reason == PoisonWorkloadTerminalLogLimit);
    mu_check(workload.usage.logs == 3u);
    mu_check(workload.console_count == 2u);
    mu_check(workload.console[1].type == PoisonWorkloadConsoleTruncation);
    mu_check(strcmp(workload.console[1].text, "[output limit reached]") == 0);
}

MU_TEST(poison_managed_workload_survives_reconnect_and_isolates_actor) {
    uint8_t actor_a[32u] = {0};
    uint8_t actor_b[32u] = {0};
    actor_a[0] = 0xa1u;
    actor_b[0] = 0xb2u;
    PoisonManagedWorkload* first =
        poison_managed_workload_attach(actor_a, "managed-reconnect", true);
    mu_check(first != NULL);
    PoisonWorkloadLimits limits = test_limits();
    mu_check(poison_workload_init(&first->workload, "managed-reconnect", digest, digest, &limits));
    mu_check(poison_workload_start(&first->workload));
    mu_check(poison_workload_force_terminate(&first->workload, PoisonWorkloadTerminalDisconnect));
    poison_managed_workload_detach(first);

    PoisonManagedWorkload* resumed =
        poison_managed_workload_attach(actor_a, "managed-reconnect", false);
    mu_check(resumed == first);
    mu_check(resumed->workload.state == PoisonWorkloadDisconnected);
    mu_check(resumed->workload.terminal_reason == PoisonWorkloadTerminalDisconnect);
    mu_check(poison_managed_workload_attach(actor_b, "managed-reconnect", false) == NULL);
    PoisonManagedWorkload* isolated =
        poison_managed_workload_attach(actor_b, "managed-reconnect", true);
    mu_check(isolated != NULL);
    mu_check(isolated != resumed);
    poison_managed_workload_detach(isolated);
    poison_managed_workload_detach(resumed);
}

MU_TEST(poison_js_bundle_metadata_is_bounded) {
    PoisonJsBundleMetadata metadata = {
        .id = "org.poisonedos.ui",
        .version = "1.0.0",
        .entrypoint = "index.js",
        .content_digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .api_version = 1,
        .size = 1024,
    };
    mu_check(poison_js_bundle_metadata_valid(&metadata));
    metadata.entrypoint[0] = '/';
    mu_check(!poison_js_bundle_metadata_valid(&metadata));
    mu_check(poison_js_bundle_path_valid("assets/app.js"));
    mu_check(poison_js_bundle_path_valid("assets/file..js"));
    mu_check(!poison_js_bundle_path_valid("../app.js"));
    mu_check(!poison_js_bundle_path_valid("assets/../app.js"));
    mu_check(!poison_js_bundle_path_valid("assets//app.js"));
    mu_check(!poison_js_bundle_path_valid("assets\\app.js"));
    memset(metadata.id, 'a', sizeof(metadata.id));
    metadata.id[sizeof(metadata.id) - 1u] = '\0';
    metadata.entrypoint[0] = 'i';
    mu_check(poison_js_bundle_metadata_valid(&metadata));
    memset(metadata.id, 'a', sizeof(metadata.id));
    mu_check(!poison_js_bundle_metadata_valid(&metadata));
}

MU_TEST(poison_native_adapter_rejects_unscoped_paths_before_loader_access) {
    PoisonWorkload workload;
    PoisonWorkloadNativeAdapter adapter = {0};
    PoisonWorkloadLimits limits = test_limits();
    mu_check(poison_workload_init(&workload, "native-1", digest, digest, &limits));
    mu_check(!poison_workload_native_start(&adapter, &workload, "../../evil.fap"));
    mu_check(workload.state == PoisonWorkloadQueued);
    mu_check(!poison_workload_native_start(&adapter, &workload, "/tmp/evil.fap"));
    mu_check(workload.state == PoisonWorkloadQueued);
}

MU_TEST(poison_workload_rpc_contract_rejects_mutated_or_unbounded_requests) {
    PB_Poison_WorkloadRequest request = PB_Poison_WorkloadRequest_init_zero;
    request.operation = PB_Poison_WorkloadOperation_WORKLOAD_OPERATION_CREATE;
    strcpy(request.workload_id, "js-1");
    request.runtime = PB_Poison_WorkloadRuntime_WORKLOAD_RUNTIME_JAVASCRIPT;
    strcpy(request.project_digest, digest);
    strcpy(request.capabilities_digest, digest);
    strcpy(request.entrypoint, "/scripts/javascript/js-1/src/main.js");
    request.has_limits = true;
    request.capability_mask = JsCapabilityStorage | JsCapabilityGpio;
    request.limits = (PB_Poison_WorkloadLimits){
        .heap_bytes = 32768u,
        .source_bytes = 65536u,
        .modules = 8u,
        .parser_depth = 32u,
        .stack_depth = 32u,
        .fuel = 100000u,
        .callbacks = 16u,
        .timers = 16u,
        .open_handles = 8u,
        .logs = 16384u,
        .artifacts = 4u,
        .wall_ms = 5000u,
        .artifact_bytes = 131072u,
    };
    mu_check(rpc_poison_workload_request_is_valid(&request, PoisonRoleOperator));
    mu_check(
        rpc_poison_workload_effective_js_capabilities(
            POISON_CAPABILITY_CONTROL | POISON_CAPABILITY_FILES, request.capability_mask) ==
        JsCapabilityStorage);
    mu_check(
        rpc_poison_workload_effective_js_capabilities(
            POISON_CAPABILITY_CONTROL | POISON_CAPABILITY_NATIVE, JsCapabilityBadUsb) == 0u);

    request.project_digest[0] = 'A';
    mu_check(!rpc_poison_workload_request_is_valid(&request, PoisonRoleOperator));
    request.project_digest[0] = '0';
    request.capability_mask = 1u << 31u;
    mu_check(!rpc_poison_workload_request_is_valid(&request, PoisonRoleOperator));
    request.capability_mask = JsCapabilityStorage | JsCapabilityGpio;
    request.limits.heap_bytes = 65537u;
    mu_check(!rpc_poison_workload_request_is_valid(&request, PoisonRoleOperator));
    request.limits.heap_bytes = 32768u;
    strcpy(request.entrypoint, "/evidence/private.js");
    mu_check(!rpc_poison_workload_request_is_valid(&request, PoisonRoleOperator));

    request = (PB_Poison_WorkloadRequest)PB_Poison_WorkloadRequest_init_zero;
    request.operation = PB_Poison_WorkloadOperation_WORKLOAD_OPERATION_INSPECT;
    strcpy(request.workload_id, "js-1");
    request.from_sequence = 4u;
    mu_check(rpc_poison_workload_request_is_valid(&request, PoisonRoleObserver));
    strcpy(request.entrypoint, "/scripts/javascript/js-1/src/main.js");
    mu_check(!rpc_poison_workload_request_is_valid(&request, PoisonRoleObserver));

    request = (PB_Poison_WorkloadRequest)PB_Poison_WorkloadRequest_init_zero;
    request.operation = PB_Poison_WorkloadOperation_WORKLOAD_OPERATION_FINALIZE_ARTIFACT;
    strcpy(request.workload_id, "js-1");
    strcpy(request.artifact_id, "report.json");
    strcpy(
        request.artifact_path,
        "/scripts/javascript/js-1/versions/0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef/artifacts/report.json");
    request.artifact_size = 32u;
    strcpy(request.artifact_sha256, digest);
    mu_check(rpc_poison_workload_request_is_valid(&request, PoisonRoleOperator));
    strcpy(request.artifact_path, "/evidence/report.json");
    mu_check(!rpc_poison_workload_request_is_valid(&request, PoisonRoleOperator));
}

MU_TEST(poison_workload_artifact_is_verified_and_committed_to_evidence) {
    static const uint8_t payload[] = "verified workload artifact";
    const char* root = "/ext/scripts/poison-unit";
    const char* artifact_root = "/ext/scripts/poison-unit/artifacts";
    const char* artifact_path = "/ext/scripts/poison-unit/artifacts/report.bin";
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FS_Error status = storage_common_mkdir(storage, "/ext/scripts");
    mu_check(status == FSE_OK || status == FSE_EXIST);
    status = storage_common_mkdir(storage, root);
    mu_check(status == FSE_OK || status == FSE_EXIST);
    status = storage_common_mkdir(storage, artifact_root);
    mu_check(status == FSE_OK || status == FSE_EXIST);
    File* file = storage_file_alloc(storage);
    mu_check(file != NULL);
    mu_check(storage_file_open(file, artifact_path, FSAM_WRITE, FSOM_CREATE_ALWAYS));
    mu_check(storage_file_write(file, payload, sizeof(payload)) == sizeof(payload));
    mu_check(storage_file_sync(file));
    mu_check(storage_file_close(file));
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    uint8_t content_sha256[32u];
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    mu_check(mbedtls_sha256_starts(&hash, 0) == 0);
    mu_check(mbedtls_sha256_update(&hash, payload, sizeof(payload)) == 0);
    mu_check(mbedtls_sha256_finish(&hash, content_sha256) == 0);
    mbedtls_sha256_free(&hash);

    char case_id[65u];
    char evidence_id[65u];
    const unsigned long nonce = (unsigned long)furi_get_tick();
    snprintf(case_id, sizeof(case_id), "js-case-%08lx", nonce);
    snprintf(evidence_id, sizeof(evidence_id), "js-evidence-%08lx", nonce);
    PoisonCaseRecord evidence_case = {0};
    strcpy(evidence_case.case_id, case_id);
    strcpy(evidence_case.name, "JavaScript artifact test");
    strcpy(evidence_case.purpose, "Verify workload evidence capture");
    strcpy(evidence_case.owner_id, "unit-test");
    evidence_case.created_at_ms = nonce + 1u;
    strcpy(evidence_case.retention_policy, "test");
    mu_check(poison_case_create_persistent(&evidence_case));

    PoisonWorkload workload;
    PoisonWorkloadLimits limits = test_limits();
    uint8_t previous_audit[32u] = {0};
    mu_check(poison_workload_init(&workload, "artifact-run", digest, digest, &limits));
    mu_check(poison_workload_start(&workload));
    mu_check(poison_workload_artifact_begin(&workload, "report.bin", artifact_path));
    mu_check(poison_workload_artifact_commit(
        &workload,
        "report.bin",
        sizeof(payload),
        content_sha256,
        true,
        evidence_id,
        case_id,
        previous_audit));
    const PoisonWorkloadArtifact* artifact =
        poison_workload_artifact_find(&workload, "report.bin");
    mu_check(artifact != NULL);
    mu_check(artifact->state == PoisonWorkloadArtifactEvidence);
    mu_check(strcmp(artifact->evidence_id, evidence_id) == 0);
    mu_check(poison_evidence_record_exists_global(evidence_id));
    memset(content_sha256, 0, sizeof(content_sha256));
}

MU_TEST_SUITE(poison_workload_suite) {
    MU_RUN_TEST(poison_workload_lifecycle_and_accounting);
    MU_RUN_TEST(poison_workload_limit_and_authorization);
    MU_RUN_TEST(poison_workload_cancel_and_artifact);
    MU_RUN_TEST(poison_workload_log_limit_is_terminal);
    MU_RUN_TEST(poison_managed_workload_survives_reconnect_and_isolates_actor);
    MU_RUN_TEST(poison_js_bundle_metadata_is_bounded);
    MU_RUN_TEST(poison_native_adapter_rejects_unscoped_paths_before_loader_access);
    MU_RUN_TEST(poison_workload_rpc_contract_rejects_mutated_or_unbounded_requests);
    MU_RUN_TEST(poison_workload_artifact_is_verified_and_committed_to_evidence);
}

void poison_workload_run_tests(void) {
    MU_RUN_SUITE(poison_workload_suite);
}
