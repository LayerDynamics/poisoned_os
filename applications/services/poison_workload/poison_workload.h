#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_WORKLOAD_MAX_ID             64u
#define POISON_WORKLOAD_MAX_DIGEST         65u
#define POISON_WORKLOAD_MAX_CONSOLE_FRAMES 32u
#define POISON_WORKLOAD_MAX_CONSOLE_TEXT   128u
#define POISON_WORKLOAD_MAX_ARTIFACTS      8u
#define POISON_WORKLOAD_MAX_ARTIFACT_ID    64u
#define POISON_WORKLOAD_MAX_ARTIFACT_PATH  257u

typedef enum {
    PoisonWorkloadQueued,
    PoisonWorkloadRunning,
    PoisonWorkloadCancelling,
    PoisonWorkloadCompleted,
    PoisonWorkloadFailed,
    PoisonWorkloadCancelled,
    PoisonWorkloadTimedOut,
    PoisonWorkloadCrashed,
    PoisonWorkloadDisconnected,
} PoisonWorkloadState;

typedef enum {
    PoisonWorkloadTerminalNone,
    PoisonWorkloadTerminalCompleted,
    PoisonWorkloadTerminalCancelled,
    PoisonWorkloadTerminalTimeout,
    PoisonWorkloadTerminalCrash,
    PoisonWorkloadTerminalDisconnect,
    PoisonWorkloadTerminalHeapLimit,
    PoisonWorkloadTerminalSourceLimit,
    PoisonWorkloadTerminalModuleLimit,
    PoisonWorkloadTerminalParserLimit,
    PoisonWorkloadTerminalStackLimit,
    PoisonWorkloadTerminalFuelLimit,
    PoisonWorkloadTerminalCallbackLimit,
    PoisonWorkloadTerminalTimerLimit,
    PoisonWorkloadTerminalHandleLimit,
    PoisonWorkloadTerminalLogLimit,
    PoisonWorkloadTerminalArtifactLimit,
} PoisonWorkloadTerminalReason;

typedef enum {
    PoisonWorkloadConsoleStdout,
    PoisonWorkloadConsoleStderr,
    PoisonWorkloadConsoleLog,
    PoisonWorkloadConsoleEvent,
    PoisonWorkloadConsoleTruncation,
} PoisonWorkloadConsoleType;

typedef struct {
    uint32_t heap_bytes;
    uint32_t source_bytes;
    uint16_t modules;
    uint16_t parser_depth;
    uint16_t stack_depth;
    uint32_t fuel;
    uint16_t callbacks;
    uint16_t timers;
    uint16_t open_handles;
    uint32_t logs;
    uint16_t artifacts;
    uint32_t wall_ms;
    uint32_t artifact_bytes;
} PoisonWorkloadLimits;

typedef struct {
    uint32_t heap_bytes;
    uint32_t source_bytes;
    uint16_t modules;
    uint16_t parser_depth;
    uint16_t stack_depth;
    uint32_t fuel;
    uint16_t callbacks;
    uint16_t timers;
    uint16_t open_handles;
    uint32_t logs;
    uint16_t artifacts;
    uint32_t wall_ms;
    uint32_t artifact_bytes;
} PoisonWorkloadUsage;

typedef enum {
    PoisonWorkloadArtifactPartial,
    PoisonWorkloadArtifactProject,
    PoisonWorkloadArtifactEvidence,
} PoisonWorkloadArtifactState;

typedef struct {
    char artifact_id[POISON_WORKLOAD_MAX_ARTIFACT_ID];
    char path[POISON_WORKLOAD_MAX_ARTIFACT_PATH];
    uint64_t size;
    uint8_t sha256[32u];
    PoisonWorkloadArtifactState state;
    char evidence_id[65u];
} PoisonWorkloadArtifact;

typedef struct {
    uint64_t sequence;
    PoisonWorkloadConsoleType type;
    char text[POISON_WORKLOAD_MAX_CONSOLE_TEXT + 1u];
} PoisonWorkloadConsoleFrame;

typedef struct {
    char workload_id[POISON_WORKLOAD_MAX_ID];
    char project_digest[POISON_WORKLOAD_MAX_DIGEST];
    char capabilities_digest[POISON_WORKLOAD_MAX_DIGEST];
    PoisonWorkloadState state;
    PoisonWorkloadTerminalReason terminal_reason;
    PoisonWorkloadLimits limits;
    PoisonWorkloadUsage usage;
    PoisonWorkloadConsoleFrame console[POISON_WORKLOAD_MAX_CONSOLE_FRAMES];
    size_t console_count;
    uint64_t next_console_sequence;
    uint16_t artifact_count;
    PoisonWorkloadArtifact artifacts[POISON_WORKLOAD_MAX_ARTIFACTS];
} PoisonWorkload;

typedef enum {
    PoisonWorkloadOperationCreate,
    PoisonWorkloadOperationRun,
    PoisonWorkloadOperationCancel,
    PoisonWorkloadOperationInspect,
    PoisonWorkloadOperationFinalizeArtifact,
} PoisonWorkloadOperation;

typedef struct {
    bool create;
    bool run;
    bool cancel;
    bool inspect;
    bool finalize_artifact;
} PoisonWorkloadAuthorization;

void poison_workload_on_system_start(void);
bool poison_workload_authorize(
    const PoisonWorkloadAuthorization* authorization,
    PoisonWorkloadOperation operation);
bool poison_workload_init(
    PoisonWorkload* workload,
    const char* workload_id,
    const char* project_digest,
    const char* capabilities_digest,
    const PoisonWorkloadLimits* limits);
bool poison_workload_start(PoisonWorkload* workload);
bool poison_workload_request_cancel(PoisonWorkload* workload);
bool poison_workload_force_terminate(PoisonWorkload* workload, PoisonWorkloadTerminalReason reason);
bool poison_workload_account(
    PoisonWorkload* workload,
    const PoisonWorkloadUsage* increment,
    PoisonWorkloadTerminalReason limit_reason);
bool poison_workload_append_console(
    PoisonWorkload* workload,
    PoisonWorkloadConsoleType type,
    const char* text);
bool poison_workload_finalize_artifact(PoisonWorkload* workload, bool evidence_requested);
bool poison_workload_finish(PoisonWorkload* workload, bool success);
const PoisonWorkload* poison_workload_get(const PoisonWorkload* workload);

#ifdef __cplusplus
}
#endif
