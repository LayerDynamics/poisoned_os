#include "poison_workload.h"
#include "poison_workload_i.h"

#include <furi.h>
#include <applications/services/poison_startup.h>
#include <stdlib.h>
#include <string.h>

static FuriMutex* poison_managed_workloads_mutex;
static PoisonManagedWorkload* poison_managed_workloads[POISON_MANAGED_WORKLOAD_MAX_RECORDS];

static bool bounded_digest(const char* value) {
    if(!value || strlen(value) != POISON_WORKLOAD_MAX_DIGEST - 1u) return false;
    for(size_t i = 0; i < POISON_WORKLOAD_MAX_DIGEST - 1u; ++i) {
        const char c = value[i];
        if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static bool bounded_id(const char* value) {
    if(!value || value[0] == '\0' || strlen(value) >= POISON_WORKLOAD_MAX_ID) return false;
    for(const char* cursor = value; *cursor; ++cursor) {
        if(!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= '0' && *cursor <= '9') ||
             *cursor == '-' || *cursor == '_'))
            return false;
    }
    return true;
}

static bool terminal_state(PoisonWorkloadState state) {
    return state >= PoisonWorkloadCompleted;
}

static PoisonWorkloadState state_for_reason(PoisonWorkloadTerminalReason reason) {
    switch(reason) {
    case PoisonWorkloadTerminalCancelled:
        return PoisonWorkloadCancelled;
    case PoisonWorkloadTerminalTimeout:
        return PoisonWorkloadTimedOut;
    case PoisonWorkloadTerminalDisconnect:
        return PoisonWorkloadDisconnected;
    case PoisonWorkloadTerminalCompleted:
        return PoisonWorkloadCompleted;
    default:
        return PoisonWorkloadFailed;
    }
}

void poison_workload_on_system_start(void) {
    if(!poison_startup_is_runtime_boot()) return;
    if(!poison_managed_workloads_mutex)
        poison_managed_workloads_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    furi_check(poison_managed_workloads_mutex);
}

bool poison_managed_workload_matches(
    const PoisonManagedWorkload* managed,
    const uint8_t actor_digest[32u],
    const char* workload_id) {
    return managed && managed->occupied && actor_digest && workload_id &&
           memcmp(managed->actor_digest, actor_digest, sizeof(managed->actor_digest)) == 0 &&
           strcmp(managed->workload.workload_id, workload_id) == 0;
}

static bool poison_managed_workload_terminal(const PoisonManagedWorkload* managed) {
    return managed && managed->occupied && managed->workload.state >= PoisonWorkloadCompleted;
}

static void poison_managed_workload_clear(PoisonManagedWorkload* managed) {
    furi_check(managed && managed->mutex);
    FuriMutex* mutex = managed->mutex;
    memset(managed, 0, sizeof(*managed));
    managed->mutex = mutex;
}

PoisonManagedWorkload* poison_managed_workload_attach(
    const uint8_t actor_digest[32u],
    const char* workload_id,
    bool create_if_missing) {
    if(!actor_digest || !bounded_id(workload_id) || !poison_managed_workloads_mutex) return NULL;
    furi_check(
        furi_mutex_acquire(poison_managed_workloads_mutex, FuriWaitForever) == FuriStatusOk);
    PoisonManagedWorkload* selected = NULL;
    size_t replacement = POISON_MANAGED_WORKLOAD_MAX_RECORDS;
    for(size_t index = 0u; index < POISON_MANAGED_WORKLOAD_MAX_RECORDS; ++index) {
        PoisonManagedWorkload* candidate = poison_managed_workloads[index];
        if(poison_managed_workload_matches(candidate, actor_digest, workload_id)) {
            selected = candidate;
            break;
        }
        if(create_if_missing && replacement == POISON_MANAGED_WORKLOAD_MAX_RECORDS &&
           (!candidate || !candidate->occupied ||
            (candidate->attachments == 0u && (candidate->workload.project_digest[0] == '\0' ||
                                              poison_managed_workload_terminal(candidate))))) {
            replacement = index;
        }
    }
    if(!selected && create_if_missing && replacement < POISON_MANAGED_WORKLOAD_MAX_RECORDS) {
        selected = poison_managed_workloads[replacement];
        if(!selected) {
            selected = calloc(1u, sizeof(*selected));
            if(selected) selected->mutex = furi_mutex_alloc(FuriMutexTypeRecursive);
            if(!selected || !selected->mutex) {
                if(selected) free(selected);
                selected = NULL;
            } else {
                poison_managed_workloads[replacement] = selected;
            }
        } else {
            poison_managed_workload_clear(selected);
        }
        if(selected) {
            selected->occupied = true;
            memcpy(selected->actor_digest, actor_digest, sizeof(selected->actor_digest));
            strncpy(
                selected->workload.workload_id,
                workload_id,
                sizeof(selected->workload.workload_id) - 1u);
        }
    }
    if(selected && selected->attachments < UINT16_MAX) {
        ++selected->attachments;
    } else if(selected) {
        selected = NULL;
    }
    furi_check(furi_mutex_release(poison_managed_workloads_mutex) == FuriStatusOk);
    return selected;
}

void poison_managed_workload_detach(PoisonManagedWorkload* managed) {
    if(!managed || !poison_managed_workloads_mutex) return;
    furi_check(
        furi_mutex_acquire(poison_managed_workloads_mutex, FuriWaitForever) == FuriStatusOk);
    furi_check(managed->attachments > 0u);
    --managed->attachments;
    furi_check(furi_mutex_release(poison_managed_workloads_mutex) == FuriStatusOk);
}

void poison_managed_workload_lock(PoisonManagedWorkload* managed) {
    furi_check(managed && managed->mutex);
    furi_check(furi_mutex_acquire(managed->mutex, FuriWaitForever) == FuriStatusOk);
}

void poison_managed_workload_unlock(PoisonManagedWorkload* managed) {
    furi_check(managed && managed->mutex);
    furi_check(furi_mutex_release(managed->mutex) == FuriStatusOk);
}

bool poison_workload_authorize(
    const PoisonWorkloadAuthorization* authorization,
    PoisonWorkloadOperation operation) {
    if(!authorization || operation > PoisonWorkloadOperationFinalizeArtifact) return false;
    switch(operation) {
    case PoisonWorkloadOperationCreate:
        return authorization->create;
    case PoisonWorkloadOperationRun:
        return authorization->run;
    case PoisonWorkloadOperationCancel:
        return authorization->cancel;
    case PoisonWorkloadOperationInspect:
        return authorization->inspect;
    case PoisonWorkloadOperationFinalizeArtifact:
        return authorization->finalize_artifact;
    default:
        return false;
    }
}

bool poison_workload_init(
    PoisonWorkload* workload,
    const char* workload_id,
    const char* project_digest,
    const char* capabilities_digest,
    const PoisonWorkloadLimits* limits) {
    if(!workload || !bounded_id(workload_id) || !bounded_digest(project_digest) ||
       !bounded_digest(capabilities_digest) || !limits || limits->heap_bytes == 0u ||
       limits->source_bytes == 0u || limits->modules == 0u || limits->fuel == 0u ||
       limits->wall_ms == 0u)
        return false;
    memset(workload, 0, sizeof(*workload));
    strncpy(workload->workload_id, workload_id, sizeof(workload->workload_id) - 1u);
    strncpy(workload->project_digest, project_digest, sizeof(workload->project_digest) - 1u);
    strncpy(
        workload->capabilities_digest,
        capabilities_digest,
        sizeof(workload->capabilities_digest) - 1u);
    workload->state = PoisonWorkloadQueued;
    workload->limits = *limits;
    workload->next_console_sequence = 1u;
    return true;
}

bool poison_workload_start(PoisonWorkload* workload) {
    if(!workload || workload->state != PoisonWorkloadQueued) return false;
    workload->state = PoisonWorkloadRunning;
    return true;
}

bool poison_workload_request_cancel(PoisonWorkload* workload) {
    if(!workload || workload->state != PoisonWorkloadRunning) return false;
    workload->state = PoisonWorkloadCancelling;
    return true;
}

bool poison_workload_force_terminate(PoisonWorkload* workload, PoisonWorkloadTerminalReason reason) {
    if(!workload || reason == PoisonWorkloadTerminalNone || terminal_state(workload->state))
        return false;
    if(workload->state != PoisonWorkloadRunning && workload->state != PoisonWorkloadCancelling)
        return false;
    workload->terminal_reason = reason;
    workload->state = state_for_reason(reason);
    return true;
}

static bool exceeds(uint32_t current, uint32_t increment, uint32_t maximum) {
    return increment > maximum || current > maximum - increment;
}

static bool exceeds16(uint16_t current, uint16_t increment, uint16_t maximum) {
    return increment > maximum || current > maximum - increment;
}

bool poison_workload_account(
    PoisonWorkload* workload,
    const PoisonWorkloadUsage* increment,
    PoisonWorkloadTerminalReason limit_reason) {
    if(!workload || !increment || workload->state != PoisonWorkloadRunning) return false;
    const bool over =
        exceeds(workload->usage.heap_bytes, increment->heap_bytes, workload->limits.heap_bytes) ||
        exceeds(
            workload->usage.source_bytes,
            increment->source_bytes,
            workload->limits.source_bytes) ||
        exceeds16(workload->usage.modules, increment->modules, workload->limits.modules) ||
        exceeds16(
            workload->usage.parser_depth,
            increment->parser_depth,
            workload->limits.parser_depth) ||
        exceeds16(
            workload->usage.stack_depth, increment->stack_depth, workload->limits.stack_depth) ||
        exceeds(workload->usage.fuel, increment->fuel, workload->limits.fuel) ||
        exceeds16(workload->usage.callbacks, increment->callbacks, workload->limits.callbacks) ||
        exceeds16(workload->usage.timers, increment->timers, workload->limits.timers) ||
        exceeds16(
            workload->usage.open_handles,
            increment->open_handles,
            workload->limits.open_handles) ||
        exceeds(workload->usage.logs, increment->logs, workload->limits.logs) ||
        exceeds16(workload->usage.artifacts, increment->artifacts, workload->limits.artifacts) ||
        exceeds(workload->usage.wall_ms, increment->wall_ms, workload->limits.wall_ms) ||
        exceeds(
            workload->usage.artifact_bytes,
            increment->artifact_bytes,
            workload->limits.artifact_bytes);
    if(over) {
        return poison_workload_force_terminate(
            workload,
            limit_reason == PoisonWorkloadTerminalNone ? PoisonWorkloadTerminalFuelLimit :
                                                         limit_reason);
    }
    workload->usage.heap_bytes += increment->heap_bytes;
    workload->usage.source_bytes += increment->source_bytes;
    workload->usage.modules += increment->modules;
    workload->usage.parser_depth += increment->parser_depth;
    workload->usage.stack_depth += increment->stack_depth;
    workload->usage.fuel += increment->fuel;
    workload->usage.callbacks += increment->callbacks;
    workload->usage.timers += increment->timers;
    workload->usage.open_handles += increment->open_handles;
    workload->usage.logs += increment->logs;
    workload->usage.artifacts += increment->artifacts;
    workload->usage.wall_ms += increment->wall_ms;
    workload->usage.artifact_bytes += increment->artifact_bytes;
    return true;
}

bool poison_workload_append_console(
    PoisonWorkload* workload,
    PoisonWorkloadConsoleType type,
    const char* text) {
    if(!workload || !text || type > PoisonWorkloadConsoleTruncation ||
       terminal_state(workload->state))
        return false;
    const size_t length = strnlen(text, POISON_WORKLOAD_MAX_CONSOLE_TEXT + 1u);
    const bool text_truncated = length > POISON_WORKLOAD_MAX_CONSOLE_TEXT;
    const char* stored_text = text_truncated ? "[truncated]" : text;
    const uint32_t stored_bytes = (uint32_t)strlen(stored_text);
    const bool frame_overflow = workload->console_count >= POISON_WORKLOAD_MAX_CONSOLE_FRAMES - 1u;
    const bool byte_overflow = stored_bytes > workload->limits.logs ||
                               workload->usage.logs > workload->limits.logs - stored_bytes;
    if(frame_overflow || byte_overflow) {
        if(workload->console_count < POISON_WORKLOAD_MAX_CONSOLE_FRAMES) {
            PoisonWorkloadConsoleFrame* truncation = &workload->console[workload->console_count++];
            truncation->sequence = workload->next_console_sequence++;
            truncation->type = PoisonWorkloadConsoleTruncation;
            strcpy(truncation->text, "[output limit reached]");
        }
        workload->usage.logs = workload->limits.logs;
        workload->terminal_reason = PoisonWorkloadTerminalLogLimit;
        workload->state = PoisonWorkloadFailed;
        return false;
    }
    PoisonWorkloadConsoleFrame* frame = &workload->console[workload->console_count++];
    frame->sequence = workload->next_console_sequence++;
    frame->type = text_truncated ? PoisonWorkloadConsoleTruncation : type;
    strcpy(frame->text, stored_text);
    workload->usage.logs += stored_bytes;
    return true;
}

bool poison_workload_finalize_artifact(PoisonWorkload* workload, bool evidence_requested) {
    if(!workload || workload->state != PoisonWorkloadRunning || !evidence_requested ||
       workload->artifact_count >= POISON_WORKLOAD_MAX_ARTIFACTS ||
       workload->usage.artifacts >= workload->limits.artifacts)
        return false;
    ++workload->artifact_count;
    ++workload->usage.artifacts;
    return true;
}

bool poison_workload_finish(PoisonWorkload* workload, bool success) {
    if(!workload ||
       (workload->state != PoisonWorkloadRunning && workload->state != PoisonWorkloadCancelling))
        return false;
    workload->terminal_reason = success ? PoisonWorkloadTerminalCompleted :
                                          PoisonWorkloadTerminalCrash;
    workload->state = success ? PoisonWorkloadCompleted : PoisonWorkloadCrashed;
    return true;
}

const PoisonWorkload* poison_workload_get(const PoisonWorkload* workload) {
    return workload;
}
