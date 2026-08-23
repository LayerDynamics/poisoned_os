#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_DIAGNOSTICS_RING_SIZE (16u)
#define POISON_DIAGNOSTICS_TEXT_MAX  (96u)

typedef enum {
    PoisonDiagnosticSessionEstablished,
    PoisonDiagnosticTransportError,
    PoisonDiagnosticDroppedFrame,
    PoisonDiagnosticRetriedFrame,
    PoisonDiagnosticCommandFailure,
    PoisonDiagnosticAppCrash,
    PoisonDiagnosticPolicyDenial,
    PoisonDiagnosticPackageVerification,
    PoisonDiagnosticPackageRevocation,
    PoisonDiagnosticUpdateStage,
    PoisonDiagnosticUpdateHealth,
    PoisonDiagnosticUpdateRollback,
    PoisonDiagnosticRecovery,
    PoisonDiagnosticJavascriptStart,
    PoisonDiagnosticJavascriptTerminal,
    PoisonDiagnosticJavascriptCrash,
    PoisonDiagnosticJavascriptLimit,
    PoisonDiagnosticJavascriptRecovery,
    PoisonDiagnosticCategoryCount,
} PoisonDiagnosticCategory;

typedef struct {
    uint64_t event_id;
    PoisonDiagnosticCategory category;
    char summary[POISON_DIAGNOSTICS_TEXT_MAX + 1u];
    uint64_t timestamp_ms;
    uint8_t correlation_digest[32];
} PoisonDiagnosticEvent;

typedef struct {
    uint32_t session_established;
    uint32_t transport_errors;
    uint32_t dropped_frames;
    uint32_t retried_frames;
    uint32_t command_failures;
    uint32_t app_crashes;
    uint32_t policy_denials;
    uint32_t package_verifications;
    uint32_t package_revocations;
    uint32_t update_stages;
    uint32_t update_health;
    uint32_t update_rollbacks;
    uint32_t recoveries;
    uint32_t javascript_starts;
    uint32_t javascript_terminals;
    uint32_t javascript_crashes;
    uint32_t javascript_limits;
    uint32_t javascript_recoveries;
} PoisonDiagnosticCounters;

typedef struct {
    PoisonDiagnosticCounters counters;
    PoisonDiagnosticEvent events[POISON_DIAGNOSTICS_RING_SIZE];
    size_t next_event;
    uint64_t next_event_id;
} PoisonDiagnostics;

void poison_diagnostics_init(PoisonDiagnostics* diagnostics);

void poison_diagnostics_increment(
    PoisonDiagnostics* diagnostics,
    PoisonDiagnosticCategory category);

bool poison_diagnostics_record(
    PoisonDiagnostics* diagnostics,
    PoisonDiagnosticCategory category,
    const char* summary,
    uint64_t timestamp_ms,
    const uint8_t correlation_digest[32]);

const PoisonDiagnosticCounters* poison_diagnostics_counters(const PoisonDiagnostics* diagnostics);
const char* poison_diagnostics_category_name(PoisonDiagnosticCategory category);

void poison_diagnostics_on_system_start(void);

PoisonDiagnostics* poison_diagnostics_get(void);

#ifdef __cplusplus
}
#endif
