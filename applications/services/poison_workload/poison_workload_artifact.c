#include "poison_workload_i.h"

#include "../poison_evidence/poison_evidence_i.h"

#include <furi.h>
#include <mbedtls/sha256.h>
#include <storage/storage.h>

#include <string.h>

static bool poison_workload_artifact_id_valid(const char* artifact_id) {
    if(!artifact_id || artifact_id[0] == '\0' || artifact_id[0] == '.' ||
       strnlen(artifact_id, POISON_WORKLOAD_MAX_ARTIFACT_ID) >= POISON_WORKLOAD_MAX_ARTIFACT_ID) {
        return false;
    }
    for(const char* cursor = artifact_id; *cursor; ++cursor) {
        const bool allowed = (*cursor >= 'a' && *cursor <= 'z') ||
                             (*cursor >= 'A' && *cursor <= 'Z') ||
                             (*cursor >= '0' && *cursor <= '9') || *cursor == '-' ||
                             *cursor == '_' || *cursor == '.';
        if(!allowed) return false;
    }
    return true;
}

static bool poison_workload_artifact_path_valid(const char* path) {
    if(!path ||
       strnlen(path, POISON_WORKLOAD_MAX_ARTIFACT_PATH) >= POISON_WORKLOAD_MAX_ARTIFACT_PATH ||
       strncmp(path, "/ext/scripts/", 13u) != 0 || !strstr(path, "/artifacts/")) {
        return false;
    }
    return !strstr(path, "/../") && !strstr(path, "/./") && !strchr(path, '\\');
}

const PoisonWorkloadArtifact*
    poison_workload_artifact_find(const PoisonWorkload* workload, const char* artifact_id) {
    if(!workload || !artifact_id) return NULL;
    for(size_t index = 0u; index < workload->artifact_count; ++index) {
        if(strcmp(workload->artifacts[index].artifact_id, artifact_id) == 0)
            return &workload->artifacts[index];
    }
    return NULL;
}

static PoisonWorkloadArtifact*
    poison_workload_artifact_find_mutable(PoisonWorkload* workload, const char* artifact_id) {
    return (PoisonWorkloadArtifact*)poison_workload_artifact_find(workload, artifact_id);
}

bool poison_workload_artifact_begin(
    PoisonWorkload* workload,
    const char* artifact_id,
    const char* project_path) {
    if(!workload || workload->state != PoisonWorkloadRunning ||
       !poison_workload_artifact_id_valid(artifact_id) ||
       !poison_workload_artifact_path_valid(project_path)) {
        return false;
    }
    PoisonWorkloadArtifact* existing =
        poison_workload_artifact_find_mutable(workload, artifact_id);
    if(existing) return strcmp(existing->path, project_path) == 0;
    if(workload->artifact_count >= POISON_WORKLOAD_MAX_ARTIFACTS ||
       workload->artifact_count >= workload->limits.artifacts) {
        (void)poison_workload_force_terminate(workload, PoisonWorkloadTerminalArtifactLimit);
        return false;
    }
    PoisonWorkloadArtifact* artifact = &workload->artifacts[workload->artifact_count];
    memset(artifact, 0, sizeof(*artifact));
    strcpy(artifact->artifact_id, artifact_id);
    strcpy(artifact->path, project_path);
    artifact->state = PoisonWorkloadArtifactPartial;
    ++workload->artifact_count;
    return true;
}

static bool poison_workload_artifact_file_matches(
    const char* path,
    uint64_t expected_size,
    const uint8_t expected_sha256[32u]) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool valid = file && storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
                 storage_file_size(file) == expected_size;
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    valid = valid && mbedtls_sha256_starts(&hash, 0) == 0;
    uint8_t buffer[256u];
    uint64_t remaining = expected_size;
    while(valid && remaining > 0u) {
        const size_t requested = remaining < sizeof(buffer) ? (size_t)remaining : sizeof(buffer);
        const size_t received = storage_file_read(file, buffer, requested);
        valid = received == requested && mbedtls_sha256_update(&hash, buffer, received) == 0;
        remaining -= received;
    }
    uint8_t actual[32u] = {0};
    if(valid) valid = mbedtls_sha256_finish(&hash, actual) == 0;
    mbedtls_sha256_free(&hash);
    if(file && storage_file_is_open(file)) storage_file_close(file);
    if(file) storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    memset(buffer, 0, sizeof(buffer));
    const bool matches = valid && memcmp(actual, expected_sha256, sizeof(actual)) == 0;
    memset(actual, 0, sizeof(actual));
    return matches;
}

bool poison_workload_artifact_commit(
    PoisonWorkload* workload,
    const char* artifact_id,
    uint64_t expected_size,
    const uint8_t expected_sha256[32u],
    bool evidence_requested,
    const char* evidence_id,
    const char* case_id,
    const uint8_t previous_audit_sha256[32u]) {
    if(!workload || workload->state != PoisonWorkloadRunning || expected_size == 0u ||
       expected_size > UINT32_MAX || !expected_sha256 ||
       (evidence_requested && (!evidence_id || !case_id || !previous_audit_sha256))) {
        return false;
    }
    PoisonWorkloadArtifact* artifact =
        poison_workload_artifact_find_mutable(workload, artifact_id);
    if(!artifact) return false;
    if(artifact->state != PoisonWorkloadArtifactPartial) {
        return artifact->size == expected_size &&
               memcmp(artifact->sha256, expected_sha256, sizeof(artifact->sha256)) == 0 &&
               artifact->state == (evidence_requested ? PoisonWorkloadArtifactEvidence :
                                                        PoisonWorkloadArtifactProject) &&
               (!evidence_requested || strcmp(artifact->evidence_id, evidence_id) == 0);
    }
    const uint32_t artifact_bytes = (uint32_t)expected_size;
    if(artifact_bytes > workload->limits.artifact_bytes ||
       workload->usage.artifact_bytes > workload->limits.artifact_bytes - artifact_bytes) {
        (void)poison_workload_force_terminate(workload, PoisonWorkloadTerminalArtifactLimit);
        return false;
    }
    if(!poison_workload_artifact_file_matches(artifact->path, expected_size, expected_sha256)) {
        return false;
    }
    if(evidence_requested) {
        PoisonEvidenceRecord captured = {0};
        if(!poison_evidence_capture_file_global(
               evidence_id,
               case_id,
               artifact->path,
               expected_size,
               expected_sha256,
               true,
               previous_audit_sha256,
               &captured)) {
            return false;
        }
        strcpy(artifact->evidence_id, captured.evidence_id);
        memset(&captured, 0, sizeof(captured));
        artifact->state = PoisonWorkloadArtifactEvidence;
    } else {
        artifact->state = PoisonWorkloadArtifactProject;
    }
    artifact->size = expected_size;
    memcpy(artifact->sha256, expected_sha256, sizeof(artifact->sha256));
    ++workload->usage.artifacts;
    workload->usage.artifact_bytes += artifact_bytes;
    return true;
}
