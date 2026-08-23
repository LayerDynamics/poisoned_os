#include "rpc_poison_workload.h"

#include "rpc_i.h"

#include "../poison_audit/poison_audit.h"
#include "../poison_audit/poison_audit_i.h"
#include "../poison_diagnostics/poison_diagnostics.h"
#include "../poison_evidence/poison_evidence_i.h"
#include "../poison_vfs/poison_vfs_paths.h"
#include "../poison_workload/poison_workload.h"
#include "../poison_workload/poison_workload_i.h"
#include "../poison_workload/poison_workload_js_adapter.h"
#include "../poison_workload/poison_workload_native_adapter.h"

#include <applications/system/js_app/js_capabilities.h>
#include <applications/system/js_app/js_modules.h>

#include <furi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POISON_WORKLOAD_RPC_MAX_HEAP_BYTES     (64u * 1024u)
#define POISON_WORKLOAD_RPC_MAX_SOURCE_BYTES   (256u * 1024u)
#define POISON_WORKLOAD_RPC_MAX_FUEL           (10000000u)
#define POISON_WORKLOAD_RPC_MAX_LOG_UNITS      (64u * 1024u)
#define POISON_WORKLOAD_RPC_MAX_WALL_MS        (60000u)
#define POISON_WORKLOAD_RPC_MAX_ARTIFACT_BYTES (8u * 1024u * 1024u)

typedef struct {
    RpcSession* session;
    PoisonManagedWorkload* managed;
    uint32_t active_command_id;
} RpcPoisonWorkload;

#define POISON_JS_CAPABILITY_KNOWN_MASK                                                     \
    (JsCapabilityDevice | JsCapabilityRuntime | JsCapabilityUi | JsCapabilityNotification | \
     JsCapabilityBadUsb | JsCapabilitySerial | JsCapabilityGpio | JsCapabilityStorage |     \
     JsCapabilityCrypto | JsCapabilityCompute | JsCapabilityEvidence)

_Static_assert(
    (int)PoisonManagedRuntimeJavaScript ==
        (int)PB_Poison_WorkloadRuntime_WORKLOAD_RUNTIME_JAVASCRIPT,
    "managed JavaScript runtime value must match the RPC contract");
_Static_assert(
    (int)PoisonManagedRuntimeNative == (int)PB_Poison_WorkloadRuntime_WORKLOAD_RUNTIME_NATIVE,
    "managed native runtime value must match the RPC contract");
_Static_assert(
    (int)PoisonManagedRuntimeWasm == (int)PB_Poison_WorkloadRuntime_WORKLOAD_RUNTIME_WASM,
    "managed Wasm runtime value must match the RPC contract");

uint32_t rpc_poison_workload_effective_js_capabilities(
    PoisonCapability session_capabilities,
    uint32_t requested_capabilities) {
    uint32_t allowed = 0u;
    if((session_capabilities & POISON_CAPABILITY_CONTROL) != 0u) {
        allowed |= JsCapabilityDevice | JsCapabilityRuntime | JsCapabilityUi |
                   JsCapabilityNotification | JsCapabilityCrypto | JsCapabilityCompute;
    }
    if((session_capabilities & POISON_CAPABILITY_FILES) != 0u) allowed |= JsCapabilityStorage;
    if((session_capabilities & POISON_CAPABILITY_EVIDENCE) != 0u) allowed |= JsCapabilityEvidence;
    if((session_capabilities & POISON_CAPABILITY_RADIO) != 0u)
        allowed |= JsCapabilitySerial | JsCapabilityGpio;
    return requested_capabilities & allowed & POISON_JS_CAPABILITY_KNOWN_MASK;
}

static bool rpc_poison_workload_digest_valid(const char* digest);

static bool rpc_poison_workload_hex_digest(const char* value, uint8_t digest[32u]) {
    if(!rpc_poison_workload_digest_valid(value)) return false;
    for(size_t index = 0u; index < 32u; ++index) {
        const char high = value[index * 2u];
        const char low = value[index * 2u + 1u];
        const uint8_t high_value = high <= '9' ? (uint8_t)(high - '0') :
                                                 (uint8_t)(high - 'a' + 10u);
        const uint8_t low_value = low <= '9' ? (uint8_t)(low - '0') : (uint8_t)(low - 'a' + 10u);
        digest[index] = (uint8_t)((high_value << 4u) | low_value);
    }
    return true;
}

static bool rpc_poison_workload_limit_reason(PoisonWorkloadTerminalReason reason) {
    return reason >= PoisonWorkloadTerminalHeapLimit &&
           reason <= PoisonWorkloadTerminalArtifactLimit;
}

static const char* rpc_poison_workload_terminal_summary(PoisonWorkloadTerminalReason reason) {
    switch(reason) {
    case PoisonWorkloadTerminalCompleted:
        return "javascript completed";
    case PoisonWorkloadTerminalCancelled:
        return "javascript cancelled";
    case PoisonWorkloadTerminalTimeout:
        return "javascript timed out";
    case PoisonWorkloadTerminalCrash:
        return "javascript crashed";
    case PoisonWorkloadTerminalDisconnect:
        return "javascript disconnected";
    case PoisonWorkloadTerminalHeapLimit:
        return "javascript heap limit";
    case PoisonWorkloadTerminalSourceLimit:
        return "javascript source limit";
    case PoisonWorkloadTerminalModuleLimit:
        return "javascript module limit";
    case PoisonWorkloadTerminalParserLimit:
        return "javascript parser limit";
    case PoisonWorkloadTerminalStackLimit:
        return "javascript stack limit";
    case PoisonWorkloadTerminalFuelLimit:
        return "javascript fuel limit";
    case PoisonWorkloadTerminalCallbackLimit:
        return "javascript callback limit";
    case PoisonWorkloadTerminalTimerLimit:
        return "javascript timer limit";
    case PoisonWorkloadTerminalHandleLimit:
        return "javascript handle limit";
    case PoisonWorkloadTerminalLogLimit:
        return "javascript log limit";
    case PoisonWorkloadTerminalArtifactLimit:
        return "javascript artifact limit";
    default:
        return "javascript terminal";
    }
}

static void rpc_poison_workload_diagnostic_record(
    RpcPoisonWorkload* rpc_workload,
    PoisonDiagnosticCategory category,
    const char* summary) {
    uint8_t correlation[32u];
    PoisonManagedWorkload* managed = rpc_workload->managed;
    if(!managed || managed->runtime != PoisonManagedRuntimeJavaScript || !managed->occupied ||
       !rpc_poison_workload_hex_digest(managed->workload.project_digest, correlation)) {
        return;
    }
    (void)poison_diagnostics_record(
        poison_diagnostics_get(),
        category,
        summary,
        ((uint64_t)furi_get_tick() * 1000u) / furi_kernel_get_tick_frequency(),
        correlation);
    memset(correlation, 0, sizeof(correlation));
}

static void rpc_poison_workload_record_start(RpcPoisonWorkload* rpc_workload) {
    PoisonManagedWorkload* managed = rpc_workload->managed;
    if(!managed || managed->diagnostics_started ||
       managed->runtime != PoisonManagedRuntimeJavaScript)
        return;
    rpc_poison_workload_diagnostic_record(
        rpc_workload, PoisonDiagnosticJavascriptStart, "javascript started");
    managed->diagnostics_started = true;
    if(managed->recovery_pending) {
        rpc_poison_workload_diagnostic_record(
            rpc_workload, PoisonDiagnosticJavascriptRecovery, "javascript recovery verified");
        managed->recovery_pending = false;
    }
}

static void rpc_poison_workload_record_terminal(RpcPoisonWorkload* rpc_workload) {
    PoisonManagedWorkload* managed = rpc_workload->managed;
    if(!managed || !managed->occupied || managed->diagnostics_terminal_recorded ||
       managed->runtime != PoisonManagedRuntimeJavaScript ||
       managed->workload.state < PoisonWorkloadCompleted) {
        return;
    }
    const PoisonWorkloadTerminalReason reason = managed->workload.terminal_reason;
    rpc_poison_workload_diagnostic_record(
        rpc_workload,
        PoisonDiagnosticJavascriptTerminal,
        rpc_poison_workload_terminal_summary(reason));
    if(reason == PoisonWorkloadTerminalCrash) {
        rpc_poison_workload_diagnostic_record(
            rpc_workload, PoisonDiagnosticJavascriptCrash, "javascript crashed");
        managed->recovery_pending = true;
    } else if(rpc_poison_workload_limit_reason(reason)) {
        rpc_poison_workload_diagnostic_record(
            rpc_workload,
            PoisonDiagnosticJavascriptLimit,
            rpc_poison_workload_terminal_summary(reason));
        managed->recovery_pending = true;
    } else if(reason == PoisonWorkloadTerminalDisconnect || reason == PoisonWorkloadTerminalTimeout) {
        managed->recovery_pending = true;
    }
    managed->diagnostics_terminal_recorded = true;
}

static bool rpc_poison_workload_digest_valid(const char* digest) {
    if(!digest || strnlen(digest, 65u) != 64u) return false;
    for(size_t index = 0u; index < 64u; ++index) {
        const char byte = digest[index];
        if(!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f'))) return false;
    }
    return true;
}

static bool rpc_poison_workload_limits_valid(const PB_Poison_WorkloadLimits* limits) {
    return limits && limits->heap_bytes > 0u &&
           limits->heap_bytes <= POISON_WORKLOAD_RPC_MAX_HEAP_BYTES && limits->source_bytes > 0u &&
           limits->source_bytes <= POISON_WORKLOAD_RPC_MAX_SOURCE_BYTES && limits->modules > 0u &&
           limits->modules <= 32u && limits->parser_depth > 0u && limits->parser_depth <= 64u &&
           limits->stack_depth > 0u && limits->stack_depth <= 64u && limits->fuel > 0u &&
           limits->fuel <= POISON_WORKLOAD_RPC_MAX_FUEL && limits->callbacks <= 64u &&
           limits->timers <= 64u && limits->open_handles <= 32u && limits->logs > 0u &&
           limits->logs <= POISON_WORKLOAD_RPC_MAX_LOG_UNITS &&
           limits->artifacts <= POISON_WORKLOAD_MAX_ARTIFACTS &&
           limits->artifact_bytes <= POISON_WORKLOAD_RPC_MAX_ARTIFACT_BYTES &&
           ((limits->artifacts == 0u) == (limits->artifact_bytes == 0u)) && limits->wall_ms > 0u &&
           limits->wall_ms <= POISON_WORKLOAD_RPC_MAX_WALL_MS;
}

static bool rpc_poison_workload_artifact_fields_empty(const PB_Poison_WorkloadRequest* request) {
    return request->artifact_id[0] == '\0' && request->artifact_path[0] == '\0' &&
           request->artifact_size == 0u && request->artifact_sha256[0] == '\0' &&
           request->evidence_id[0] == '\0' && request->case_id[0] == '\0';
}

static bool rpc_poison_workload_artifact_request_valid(
    const PB_Poison_WorkloadRequest* request,
    PoisonRole role) {
    PoisonVfsResolvedPath artifact;
    const bool evidence_fields_valid = request->evidence_requested ?
                                           poison_evidence_id_validate(request->evidence_id) &&
                                               poison_evidence_id_validate(request->case_id) :
                                           request->evidence_id[0] == '\0' &&
                                               request->case_id[0] == '\0';
    return request->runtime == PB_Poison_WorkloadRuntime_WORKLOAD_RUNTIME_UNSPECIFIED &&
           request->project_digest[0] == '\0' && request->capabilities_digest[0] == '\0' &&
           request->entrypoint[0] == '\0' && !request->has_limits &&
           request->from_sequence == 0u && request->artifact_id[0] != '\0' &&
           request->artifact_size > 0u &&
           request->artifact_size <= POISON_WORKLOAD_RPC_MAX_ARTIFACT_BYTES &&
           rpc_poison_workload_digest_valid(request->artifact_sha256) && evidence_fields_valid &&
           poison_vfs_resolve_path(
               request->artifact_path, role, PoisonVfsOperationRead, &artifact) &&
           artifact.vfs_namespace == PoisonVfsNamespaceScripts &&
           strstr(artifact.backing_path, "/artifacts/") != NULL;
}

static bool rpc_poison_workload_suffix(const char* path, const char* suffix) {
    const size_t path_length = strlen(path);
    const size_t suffix_length = strlen(suffix);
    return path_length > suffix_length && strcmp(path + path_length - suffix_length, suffix) == 0;
}

static bool rpc_poison_workload_resolve_entrypoint(
    const PB_Poison_WorkloadRequest* request,
    PoisonRole role,
    PoisonVfsResolvedPath* resolved) {
    if(!poison_vfs_resolve_path(request->entrypoint, role, PoisonVfsOperationRead, resolved)) {
        return false;
    }
    switch(request->runtime) {
    case PB_Poison_WorkloadRuntime_WORKLOAD_RUNTIME_JAVASCRIPT:
        return resolved->vfs_namespace == PoisonVfsNamespaceScripts &&
               rpc_poison_workload_suffix(resolved->backing_path, ".js");
    case PB_Poison_WorkloadRuntime_WORKLOAD_RUNTIME_NATIVE:
        return resolved->vfs_namespace == PoisonVfsNamespaceApps &&
               rpc_poison_workload_suffix(resolved->backing_path, ".fap");
    case PB_Poison_WorkloadRuntime_WORKLOAD_RUNTIME_WASM:
        return resolved->vfs_namespace == PoisonVfsNamespaceWorkloads &&
               rpc_poison_workload_suffix(resolved->backing_path, ".wasm");
    default:
        return false;
    }
}

bool rpc_poison_workload_request_is_valid(
    const PB_Poison_WorkloadRequest* request,
    PoisonRole role) {
    if(!request || role >= PoisonRoleCount || request->workload_id[0] == '\0' ||
       strnlen(request->workload_id, sizeof(request->workload_id)) >=
           sizeof(request->workload_id) ||
       request->operation < PB_Poison_WorkloadOperation_WORKLOAD_OPERATION_CREATE ||
       request->operation > PB_Poison_WorkloadOperation_WORKLOAD_OPERATION_FINALIZE_ARTIFACT) {
        return false;
    }
    if(request->operation == PB_Poison_WorkloadOperation_WORKLOAD_OPERATION_CREATE) {
        PoisonVfsResolvedPath resolved;
        return request->has_limits && rpc_poison_workload_limits_valid(&request->limits) &&
               (request->capability_mask & ~POISON_JS_CAPABILITY_KNOWN_MASK) == 0u &&
               (request->runtime == PB_Poison_WorkloadRuntime_WORKLOAD_RUNTIME_JAVASCRIPT ||
                request->capability_mask == 0u) &&
               rpc_poison_workload_digest_valid(request->project_digest) &&
               rpc_poison_workload_digest_valid(request->capabilities_digest) &&
               rpc_poison_workload_resolve_entrypoint(request, role, &resolved) &&
               request->from_sequence == 0u && !request->evidence_requested &&
               rpc_poison_workload_artifact_fields_empty(request);
    }
    if(request->operation == PB_Poison_WorkloadOperation_WORKLOAD_OPERATION_FINALIZE_ARTIFACT) {
        return rpc_poison_workload_artifact_request_valid(request, role);
    }
    if(request->project_digest[0] != '\0' || request->capabilities_digest[0] != '\0' ||
       request->entrypoint[0] != '\0' || request->has_limits ||
       request->runtime != PB_Poison_WorkloadRuntime_WORKLOAD_RUNTIME_UNSPECIFIED ||
       request->capability_mask != 0u) {
        return false;
    }
    if(!rpc_poison_workload_artifact_fields_empty(request)) return false;
    if(request->operation == PB_Poison_WorkloadOperation_WORKLOAD_OPERATION_INSPECT)
        return !request->evidence_requested;
    if(request->operation == PB_Poison_WorkloadOperation_WORKLOAD_OPERATION_CANCEL)
        return request->from_sequence == 0u && !request->evidence_requested;
    return request->from_sequence == 0u;
}

static PoisonWorkloadOperation
    rpc_poison_workload_operation(PB_Poison_WorkloadOperation operation) {
    switch(operation) {
    case PB_Poison_WorkloadOperation_WORKLOAD_OPERATION_CREATE:
        return PoisonWorkloadOperationCreate;
    case PB_Poison_WorkloadOperation_WORKLOAD_OPERATION_RUN:
        return PoisonWorkloadOperationRun;
    case PB_Poison_WorkloadOperation_WORKLOAD_OPERATION_CANCEL:
        return PoisonWorkloadOperationCancel;
    case PB_Poison_WorkloadOperation_WORKLOAD_OPERATION_FINALIZE_ARTIFACT:
        return PoisonWorkloadOperationFinalizeArtifact;
    default:
        return PoisonWorkloadOperationInspect;
    }
}

static PoisonWorkloadAuthorization
    rpc_poison_workload_authorization(PoisonCapability capabilities) {
    const bool control = (capabilities & POISON_CAPABILITY_CONTROL) != 0u;
    return (PoisonWorkloadAuthorization){
        .create = control,
        .run = control,
        .cancel = control,
        .inspect = (capabilities & POISON_CAPABILITY_STATUS) != 0u,
        .finalize_artifact = control,
    };
}

static PoisonWorkloadLimits rpc_poison_workload_limits(const PB_Poison_WorkloadLimits* input) {
    return (PoisonWorkloadLimits){
        .heap_bytes = input->heap_bytes,
        .source_bytes = input->source_bytes,
        .modules = (uint16_t)input->modules,
        .parser_depth = (uint16_t)input->parser_depth,
        .stack_depth = (uint16_t)input->stack_depth,
        .fuel = input->fuel,
        .callbacks = (uint16_t)input->callbacks,
        .timers = (uint16_t)input->timers,
        .open_handles = (uint16_t)input->open_handles,
        .logs = input->logs,
        .artifacts = (uint16_t)input->artifacts,
        .wall_ms = input->wall_ms,
        .artifact_bytes = input->artifact_bytes,
    };
}

static PB_Poison_WorkloadState rpc_poison_workload_state(PoisonWorkloadState state) {
    return (PB_Poison_WorkloadState)(state + 1u);
}

static PB_Poison_WorkloadUsage rpc_poison_workload_usage(const PoisonWorkloadUsage* usage) {
    return (PB_Poison_WorkloadUsage){
        .heap_bytes = usage->heap_bytes,
        .source_bytes = usage->source_bytes,
        .modules = usage->modules,
        .parser_depth = usage->parser_depth,
        .stack_depth = usage->stack_depth,
        .fuel = usage->fuel,
        .callbacks = usage->callbacks,
        .timers = usage->timers,
        .open_handles = usage->open_handles,
        .logs = usage->logs,
        .artifacts = usage->artifacts,
        .wall_ms = usage->wall_ms,
        .artifact_bytes = usage->artifact_bytes,
    };
}

static void rpc_poison_workload_hex_encode(const uint8_t input[32u], char output[65u]) {
    static const char digits[] = "0123456789abcdef";
    for(size_t index = 0u; index < 32u; ++index) {
        output[index * 2u] = digits[input[index] >> 4u];
        output[index * 2u + 1u] = digits[input[index] & 0x0fu];
    }
    output[64u] = '\0';
}

static void rpc_poison_workload_status(
    RpcPoisonWorkload* rpc_workload,
    uint32_t command_id,
    const char* requested_workload_id,
    bool accepted,
    const char* message) {
    PB_Main response = PB_Main_init_zero;
    response.command_id = command_id;
    response.command_status = PB_CommandStatus_OK;
    response.which_content = PB_Main_poison_workload_status_tag;
    PB_Poison_WorkloadStatus* status = &response.content.poison_workload_status;
    PoisonManagedWorkload* managed = rpc_workload->managed;
    if(managed && managed->occupied && requested_workload_id &&
       strcmp(managed->workload.workload_id, requested_workload_id) == 0) {
        const PoisonWorkload* workload = poison_workload_get(&managed->workload);
        strcpy(status->workload_id, workload->workload_id);
        status->runtime = (PB_Poison_WorkloadRuntime)managed->runtime;
        status->state = rpc_poison_workload_state(workload->state);
        status->terminal_reason = (PB_Poison_WorkloadTerminalReason)workload->terminal_reason;
        status->has_usage = true;
        status->usage = rpc_poison_workload_usage(&workload->usage);
        status->next_sequence = workload->next_console_sequence;
        status->artifact_count = workload->artifact_count;
        status->effective_capability_mask = managed->effective_js_capabilities;
        if(workload->artifact_count > 0u) {
            const PoisonWorkloadArtifact* source =
                &workload->artifacts[workload->artifact_count - 1u];
            status->has_artifact = true;
            strcpy(status->artifact.artifact_id, source->artifact_id);
            strcpy(status->artifact.path, source->path);
            status->artifact.size = source->size;
            rpc_poison_workload_hex_encode(source->sha256, status->artifact.sha256);
            status->artifact.state = (PB_Poison_WorkloadArtifactState)(source->state + 1u);
            strcpy(status->artifact.evidence_id, source->evidence_id);
        }
    }
    status->accepted = accepted;
    if(message) snprintf(status->message, sizeof(status->message), "%s", message);
    rpc_send_and_release(rpc_workload->session, &response);
}

static void rpc_poison_workload_console(
    RpcPoisonWorkload* rpc_workload,
    uint32_t command_id,
    uint64_t from_sequence) {
    const PoisonWorkload* workload = poison_workload_get(&rpc_workload->managed->workload);
    for(size_t index = 0u; index < workload->console_count; ++index) {
        const PoisonWorkloadConsoleFrame* source = &workload->console[index];
        if(source->sequence < from_sequence) continue;
        PB_Main response = PB_Main_init_zero;
        response.command_id = command_id;
        response.command_status = PB_CommandStatus_OK;
        response.has_next = true;
        response.which_content = PB_Main_poison_workload_console_tag;
        PB_Poison_WorkloadConsoleFrame* frame = &response.content.poison_workload_console;
        strcpy(frame->workload_id, workload->workload_id);
        frame->sequence = source->sequence;
        frame->type = (PB_Poison_WorkloadConsoleType)source->type;
        strcpy(frame->text, source->text);
        rpc_send_and_release(rpc_workload->session, &response);
    }
}

static void rpc_poison_workload_audit(
    RpcPoisonWorkload* rpc_workload,
    uint32_t command_id,
    const char* action,
    const char* project_digest,
    const char* capabilities_digest,
    bool accepted) {
    uint8_t actor[POISON_AUDIT_DIGEST_BYTES];
    uint8_t correlation[POISON_AUDIT_DIGEST_BYTES];
    if(rpc_session_get_audit_context(rpc_workload->session, command_id, actor, correlation)) {
        PoisonAuditEvent event;
        char metadata[POISON_AUDIT_METADATA_MAX + 1u];
        snprintf(
            metadata,
            sizeof(metadata),
            "p=%s;c=%s;m=%03lx;r=%s",
            project_digest && project_digest[0] != '\0' ?
                project_digest :
            rpc_workload->managed && rpc_workload->managed->occupied ?
                rpc_workload->managed->workload.project_digest :
                "invalid",
            capabilities_digest && capabilities_digest[0] != '\0' ?
                capabilities_digest :
            rpc_workload->managed && rpc_workload->managed->occupied ?
                rpc_workload->managed->workload.capabilities_digest :
                "invalid",
            (unsigned long)(rpc_workload->managed ?
                                rpc_workload->managed->effective_js_capabilities :
                                0u),
            accepted ? "accepted" : "rejected");
        (void)poison_audit_append(
            poison_audit_get(),
            actor,
            action,
            "workload",
            accepted ? PoisonAuditDecisionAllowed : PoisonAuditDecisionDenied,
            ((uint64_t)furi_get_tick() * 1000u) / furi_kernel_get_tick_frequency(),
            correlation,
            metadata,
            &event);
        memset(&event, 0, sizeof(event));
    }
    memset(actor, 0, sizeof(actor));
    memset(correlation, 0, sizeof(correlation));
}

static void rpc_poison_workload_cleanup_adapter(RpcPoisonWorkload* rpc_workload) {
    PoisonManagedWorkload* managed = rpc_workload->managed;
    if(!managed) return;
    if(managed->runtime == PoisonManagedRuntimeJavaScript) {
        poison_workload_js_cleanup(&managed->js);
    } else if(managed->runtime == PoisonManagedRuntimeNative) {
        poison_workload_native_cleanup(&managed->native);
    }
}

static bool rpc_poison_workload_cancel_active(RpcPoisonWorkload* rpc_workload) {
    PoisonManagedWorkload* managed = rpc_workload->managed;
    if(!managed || !managed->occupied) return false;
    const PoisonWorkload* workload = poison_workload_get(&managed->workload);
    if(workload->state == PoisonWorkloadCancelled) return true;
    if(workload->state != PoisonWorkloadRunning && workload->state != PoisonWorkloadCancelling)
        return false;
    if(managed->runtime == PoisonManagedRuntimeJavaScript)
        return poison_workload_js_cancel(&managed->js);
    if(managed->runtime == PoisonManagedRuntimeNative)
        return poison_workload_native_cancel(&managed->native);
    return poison_workload_force_terminate(&managed->workload, PoisonWorkloadTerminalCancelled);
}

static bool
    rpc_poison_workload_cancel_callback(uint32_t command_id, const char* reason, void* context) {
    RpcPoisonWorkload* rpc_workload = context;
    UNUSED(reason);
    return rpc_workload && rpc_workload->active_command_id == command_id &&
           rpc_poison_workload_cancel_active(rpc_workload);
}

static bool rpc_poison_workload_create(
    RpcPoisonWorkload* rpc_workload,
    const PB_Poison_WorkloadRequest* request,
    PoisonRole role,
    uint32_t effective_js_capabilities) {
    PoisonManagedWorkload* managed = rpc_workload->managed;
    if(!managed) return false;
    if(managed->workload.project_digest[0] != '\0') {
        rpc_poison_workload_record_terminal(rpc_workload);
        const PoisonWorkload* current = poison_workload_get(&managed->workload);
        if(strcmp(current->workload_id, request->workload_id) == 0 &&
           strcmp(current->project_digest, request->project_digest) == 0 &&
           strcmp(current->capabilities_digest, request->capabilities_digest) == 0 &&
           managed->runtime == (PoisonManagedRuntime)request->runtime &&
           managed->effective_js_capabilities == effective_js_capabilities &&
           current->state == PoisonWorkloadQueued) {
            return true;
        }
        if(current->state < PoisonWorkloadCompleted) return false;
        rpc_poison_workload_cleanup_adapter(rpc_workload);
        memset(&managed->workload, 0, sizeof(managed->workload));
    }
    PoisonVfsResolvedPath resolved;
    if(!rpc_poison_workload_resolve_entrypoint(request, role, &resolved)) return false;
    if(request->runtime == PB_Poison_WorkloadRuntime_WORKLOAD_RUNTIME_JAVASCRIPT &&
       !js_modules_verify_managed_project(
           resolved.backing_path,
           request->project_digest,
           request->limits.source_bytes,
           request->limits.modules)) {
        return false;
    }
    const PoisonWorkloadLimits limits = rpc_poison_workload_limits(&request->limits);
    if(!poison_workload_init(
           &managed->workload,
           request->workload_id,
           request->project_digest,
           request->capabilities_digest,
           &limits)) {
        return false;
    }
    managed->runtime = (PoisonManagedRuntime)request->runtime;
    strcpy(managed->entrypoint, resolved.backing_path);
    rpc_workload->active_command_id = 0u;
    managed->diagnostics_started = false;
    managed->diagnostics_terminal_recorded = false;
    managed->evidence_requested = false;
    managed->effective_js_capabilities = effective_js_capabilities;
    return true;
}

static bool rpc_poison_workload_run(
    RpcPoisonWorkload* rpc_workload,
    const PB_Poison_WorkloadRequest* request,
    PoisonCapability capabilities,
    uint32_t command_id) {
    PoisonManagedWorkload* managed = rpc_workload->managed;
    if(!managed || strcmp(managed->workload.workload_id, request->workload_id) != 0) {
        return false;
    }
    if(managed->workload.state == PoisonWorkloadRunning) return true;
    bool started = false;
    if(managed->runtime == PoisonManagedRuntimeJavaScript) {
        started = poison_workload_js_start_managed(
            &managed->js,
            &managed->workload,
            managed->entrypoint,
            managed->effective_js_capabilities,
            managed->mutex);
    } else if(
        managed->runtime == PoisonManagedRuntimeNative &&
        (capabilities & POISON_CAPABILITY_NATIVE) != 0u) {
        started = poison_workload_native_start(
            &managed->native, &managed->workload, managed->entrypoint);
    }
    if(started) {
        managed->evidence_requested = request->evidence_requested;
        rpc_poison_workload_record_start(rpc_workload);
        rpc_workload->active_command_id = command_id;
        if(!rpc_session_register_cancellable(
               rpc_workload->session,
               command_id,
               rpc_poison_workload_cancel_callback,
               rpc_workload)) {
            poison_managed_workload_unlock(managed);
            (void)rpc_poison_workload_cancel_active(rpc_workload);
            poison_managed_workload_lock(managed);
            rpc_workload->active_command_id = 0u;
            started = false;
        }
    }
    return started;
}

static bool rpc_poison_workload_artifact_within_project(
    const RpcPoisonWorkload* rpc_workload,
    const char* artifact_path) {
    if(!rpc_workload->managed) return false;
    const char* entrypoint = rpc_workload->managed->entrypoint;
    const char* versions = strstr(entrypoint, "/versions/");
    if(!versions) return false;
    const char* digest = versions + strlen("/versions/");
    if(strlen(digest) < 65u || digest[64u] != '/') return false;
    const size_t root_length = (size_t)(digest - entrypoint) + 64u;
    return strncmp(artifact_path, entrypoint, root_length) == 0 &&
           strncmp(artifact_path + root_length, "/artifacts/", 11u) == 0;
}

static bool rpc_poison_workload_finalize_artifact(
    RpcPoisonWorkload* rpc_workload,
    const PB_Poison_WorkloadRequest* request,
    PoisonRole role) {
    PoisonManagedWorkload* managed = rpc_workload->managed;
    if(!managed || strcmp(managed->workload.workload_id, request->workload_id) != 0 ||
       managed->workload.state != PoisonWorkloadRunning ||
       (request->evidence_requested && !managed->evidence_requested) ||
       (request->evidence_requested && !poison_case_exists_persistent(request->case_id))) {
        return false;
    }
    PoisonVfsResolvedPath resolved;
    if(!poison_vfs_resolve_path(request->artifact_path, role, PoisonVfsOperationRead, &resolved) ||
       !rpc_poison_workload_artifact_within_project(rpc_workload, resolved.backing_path)) {
        return false;
    }
    uint8_t digest[32u];
    if(!rpc_poison_workload_hex_digest(request->artifact_sha256, digest)) return false;
    uint8_t previous_audit[POISON_AUDIT_DIGEST_BYTES] = {0};
    PoisonAuditEvent ignored;
    bool truncated = false;
    uint64_t next_event = 0u;
    (void)poison_audit_snapshot(
        poison_audit_get(), UINT64_MAX, &ignored, 1u, &truncated, &next_event, previous_audit);
    const bool committed = poison_workload_artifact_begin(
                               &managed->workload, request->artifact_id, resolved.backing_path) &&
                           poison_workload_artifact_commit(
                               &managed->workload,
                               request->artifact_id,
                               request->artifact_size,
                               digest,
                               request->evidence_requested,
                               request->evidence_id,
                               request->case_id,
                               previous_audit);
    memset(digest, 0, sizeof(digest));
    memset(previous_audit, 0, sizeof(previous_audit));
    memset(&ignored, 0, sizeof(ignored));
    return committed;
}

static bool rpc_poison_workload_bind(
    RpcPoisonWorkload* rpc_workload,
    const uint8_t actor_digest[32u],
    const char* workload_id,
    bool create_if_missing) {
    if(poison_managed_workload_matches(rpc_workload->managed, actor_digest, workload_id))
        return true;
    if(rpc_workload->managed) {
        poison_managed_workload_lock(rpc_workload->managed);
        const bool active = rpc_workload->managed->workload.state < PoisonWorkloadCompleted;
        poison_managed_workload_unlock(rpc_workload->managed);
        if(active) return false;
    }
    PoisonManagedWorkload* selected =
        poison_managed_workload_attach(actor_digest, workload_id, create_if_missing);
    if(!selected) return false;
    if(rpc_workload->managed) poison_managed_workload_detach(rpc_workload->managed);
    rpc_workload->managed = selected;
    rpc_workload->active_command_id = 0u;
    return true;
}

static void rpc_poison_workload_process(const PB_Main* request, void* context) {
    RpcPoisonWorkload* rpc_workload = context;
    if(!rpc_workload || request->which_content != PB_Main_poison_workload_request_tag ||
       request->has_next || !rpc_session_is_secure_dispatch_active(rpc_workload->session)) {
        rpc_send_and_release_empty(
            rpc_workload ? rpc_workload->session : NULL,
            request->command_id,
            PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    const PB_Poison_WorkloadRequest* input = &request->content.poison_workload_request;
    uint64_t session_id = 0u;
    uint8_t actor_digest[32u] = {0};
    PoisonRole role = PoisonRoleCount;
    PoisonCapability capabilities = 0u;
    bool accepted = rpc_session_get_secure_authorization(
                        rpc_workload->session, &session_id, &role, &capabilities) &&
                    rpc_session_get_secure_actor_identity(rpc_workload->session, actor_digest) &&
                    rpc_poison_workload_request_is_valid(input, role);
    UNUSED(session_id);
    const PoisonWorkloadOperation operation = rpc_poison_workload_operation(input->operation);
    const PoisonWorkloadAuthorization authorization =
        rpc_poison_workload_authorization(capabilities);
    accepted = accepted && poison_workload_authorize(&authorization, operation);
    accepted = accepted && rpc_poison_workload_bind(
                               rpc_workload,
                               actor_digest,
                               input->workload_id,
                               operation == PoisonWorkloadOperationCreate);
    bool managed_locked = rpc_workload->managed != NULL;
    if(managed_locked) poison_managed_workload_lock(rpc_workload->managed);
    const char* action = "workload.inspect";
    const char* message = "accepted";
    if(accepted && operation == PoisonWorkloadOperationCreate) {
        action = "workload.create";
        const uint32_t effective_js_capabilities =
            input->runtime == PB_Poison_WorkloadRuntime_WORKLOAD_RUNTIME_JAVASCRIPT ?
                rpc_poison_workload_effective_js_capabilities(
                    capabilities, input->capability_mask) :
                0u;
        accepted =
            rpc_poison_workload_create(rpc_workload, input, role, effective_js_capabilities);
    } else if(accepted && operation == PoisonWorkloadOperationRun) {
        action = "workload.run";
        accepted = rpc_poison_workload_run(rpc_workload, input, capabilities, request->command_id);
        if(!accepted && rpc_workload->managed &&
           rpc_workload->managed->runtime == PoisonManagedRuntimeWasm) {
            message = "measured Wasm runtime unavailable";
        }
    } else if(accepted && operation == PoisonWorkloadOperationCancel) {
        action = "workload.cancel";
        if(managed_locked) {
            poison_managed_workload_unlock(rpc_workload->managed);
            managed_locked = false;
        }
        accepted = rpc_workload->managed &&
                   strcmp(rpc_workload->managed->workload.workload_id, input->workload_id) == 0 &&
                   rpc_poison_workload_cancel_active(rpc_workload);
        if(rpc_workload->managed) {
            poison_managed_workload_lock(rpc_workload->managed);
            managed_locked = true;
        }
    } else if(accepted && operation == PoisonWorkloadOperationFinalizeArtifact) {
        action = "workload.artifact";
        accepted = rpc_poison_workload_finalize_artifact(rpc_workload, input, role);
    } else if(accepted) {
        accepted = rpc_workload->managed &&
                   strcmp(rpc_workload->managed->workload.workload_id, input->workload_id) == 0 &&
                   input->from_sequence <= rpc_workload->managed->workload.next_console_sequence;
    }
    if(!accepted && strcmp(message, "accepted") == 0) message = "rejected";
    rpc_poison_workload_audit(
        rpc_workload,
        request->command_id,
        action,
        input->project_digest,
        input->capabilities_digest,
        accepted);
    if(operation == PoisonWorkloadOperationInspect && accepted)
        rpc_poison_workload_console(rpc_workload, request->command_id, input->from_sequence);
    rpc_poison_workload_record_terminal(rpc_workload);
    rpc_poison_workload_status(
        rpc_workload, request->command_id, input->workload_id, accepted, message);
    if(rpc_workload->managed && rpc_workload->active_command_id != 0u &&
       rpc_workload->managed->workload.state >= PoisonWorkloadCompleted) {
        rpc_session_complete_cancellable(
            rpc_workload->session, rpc_workload->active_command_id, rpc_workload);
        rpc_workload->active_command_id = 0u;
    }
    if(managed_locked) poison_managed_workload_unlock(rpc_workload->managed);
    memset(actor_digest, 0, sizeof(actor_digest));
}

void* rpc_system_poison_workload_alloc(RpcSession* session) {
    RpcPoisonWorkload* rpc_workload = malloc(sizeof(*rpc_workload));
    furi_check(rpc_workload);
    memset(rpc_workload, 0, sizeof(*rpc_workload));
    rpc_workload->session = session;
    RpcHandler handler = {
        .message_handler = rpc_poison_workload_process,
        .decode_submessage = NULL,
        .context = rpc_workload,
    };
    rpc_add_handler(session, PB_Main_poison_workload_request_tag, &handler);
    return rpc_workload;
}

void rpc_system_poison_workload_free(void* context) {
    RpcPoisonWorkload* rpc_workload = context;
    if(!rpc_workload) return;
    if(rpc_workload->managed) {
        poison_managed_workload_lock(rpc_workload->managed);
        if(rpc_workload->managed->workload.state < PoisonWorkloadCompleted) {
            (void)poison_workload_force_terminate(
                &rpc_workload->managed->workload, PoisonWorkloadTerminalDisconnect);
        }
        rpc_poison_workload_record_terminal(rpc_workload);
        poison_managed_workload_unlock(rpc_workload->managed);
    }
    if(rpc_workload->active_command_id != 0u) {
        rpc_session_complete_cancellable(
            rpc_workload->session, rpc_workload->active_command_id, rpc_workload);
    }
    rpc_poison_workload_cleanup_adapter(rpc_workload);
    if(rpc_workload->managed) poison_managed_workload_detach(rpc_workload->managed);
    memset(rpc_workload, 0, sizeof(*rpc_workload));
    free(rpc_workload);
}
