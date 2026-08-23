#pragma once

#include "poison_workload.h"
#include "poison_workload_js_adapter.h"
#include "poison_workload_native_adapter.h"

#include <furi.h>

#define POISON_MANAGED_WORKLOAD_MAX_RECORDS (2u)

typedef enum {
    PoisonManagedRuntimeUnspecified,
    PoisonManagedRuntimeJavaScript,
    PoisonManagedRuntimeNative,
    PoisonManagedRuntimeWasm,
} PoisonManagedRuntime;

typedef struct {
    bool occupied;
    uint16_t attachments;
    uint8_t actor_digest[32u];
    PoisonWorkload workload;
    PoisonWorkloadJsAdapter js;
    PoisonWorkloadNativeAdapter native;
    PoisonManagedRuntime runtime;
    char entrypoint[POISON_WORKLOAD_MAX_ARTIFACT_PATH];
    uint32_t effective_js_capabilities;
    bool diagnostics_started;
    bool diagnostics_terminal_recorded;
    bool recovery_pending;
    bool evidence_requested;
    FuriMutex* mutex;
} PoisonManagedWorkload;

PoisonManagedWorkload* poison_managed_workload_attach(
    const uint8_t actor_digest[32u],
    const char* workload_id,
    bool create_if_missing);
void poison_managed_workload_detach(PoisonManagedWorkload* managed);
bool poison_managed_workload_matches(
    const PoisonManagedWorkload* managed,
    const uint8_t actor_digest[32u],
    const char* workload_id);
void poison_managed_workload_lock(PoisonManagedWorkload* managed);
void poison_managed_workload_unlock(PoisonManagedWorkload* managed);

bool poison_workload_js_start_managed(
    PoisonWorkloadJsAdapter* adapter,
    PoisonWorkload* workload,
    const char* script_path,
    uint32_t granted_capabilities,
    FuriMutex* mutex);

bool poison_workload_artifact_begin(
    PoisonWorkload* workload,
    const char* artifact_id,
    const char* project_path);
bool poison_workload_artifact_commit(
    PoisonWorkload* workload,
    const char* artifact_id,
    uint64_t expected_size,
    const uint8_t expected_sha256[32u],
    bool evidence_requested,
    const char* evidence_id,
    const char* case_id,
    const uint8_t previous_audit_sha256[32u]);
const PoisonWorkloadArtifact*
    poison_workload_artifact_find(const PoisonWorkload* workload, const char* artifact_id);
