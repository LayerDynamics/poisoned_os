#include "poison_diagnostics.h"

#include <string.h>

static PoisonDiagnostics poison_diagnostics;

static uint32_t* poison_diagnostics_counter(
    PoisonDiagnosticCounters* counters,
    PoisonDiagnosticCategory category) {
    switch(category) {
    case PoisonDiagnosticSessionEstablished:
        return &counters->session_established;
    case PoisonDiagnosticTransportError:
        return &counters->transport_errors;
    case PoisonDiagnosticDroppedFrame:
        return &counters->dropped_frames;
    case PoisonDiagnosticRetriedFrame:
        return &counters->retried_frames;
    case PoisonDiagnosticCommandFailure:
        return &counters->command_failures;
    case PoisonDiagnosticAppCrash:
        return &counters->app_crashes;
    case PoisonDiagnosticPolicyDenial:
        return &counters->policy_denials;
    case PoisonDiagnosticPackageVerification:
        return &counters->package_verifications;
    case PoisonDiagnosticPackageRevocation:
        return &counters->package_revocations;
    case PoisonDiagnosticUpdateStage:
        return &counters->update_stages;
    case PoisonDiagnosticUpdateHealth:
        return &counters->update_health;
    case PoisonDiagnosticUpdateRollback:
        return &counters->update_rollbacks;
    case PoisonDiagnosticRecovery:
        return &counters->recoveries;
    case PoisonDiagnosticJavascriptStart:
        return &counters->javascript_starts;
    case PoisonDiagnosticJavascriptTerminal:
        return &counters->javascript_terminals;
    case PoisonDiagnosticJavascriptCrash:
        return &counters->javascript_crashes;
    case PoisonDiagnosticJavascriptLimit:
        return &counters->javascript_limits;
    case PoisonDiagnosticJavascriptRecovery:
        return &counters->javascript_recoveries;
    default:
        return NULL;
    }
}

void poison_diagnostics_init(PoisonDiagnostics* diagnostics) {
    if(!diagnostics) return;
    memset(diagnostics, 0, sizeof(*diagnostics));
    diagnostics->next_event_id = 1;
}

void poison_diagnostics_increment(
    PoisonDiagnostics* diagnostics,
    PoisonDiagnosticCategory category) {
    if(!diagnostics) return;
    uint32_t* counter = poison_diagnostics_counter(&diagnostics->counters, category);
    if(counter && *counter != UINT32_MAX) ++*counter;
}

bool poison_diagnostics_record(
    PoisonDiagnostics* diagnostics,
    PoisonDiagnosticCategory category,
    const char* summary,
    uint64_t timestamp_ms,
    const uint8_t correlation_digest[32]) {
    if(!diagnostics || !summary || !correlation_digest ||
       category >= PoisonDiagnosticCategoryCount || summary[0] == '\0' ||
       strnlen(summary, POISON_DIAGNOSTICS_TEXT_MAX + 1u) > POISON_DIAGNOSTICS_TEXT_MAX) {
        return false;
    }
    const char* forbidden[] = {"secret", "key", "token", "payload", "private"};
    for(size_t index = 0; index < sizeof(forbidden) / sizeof(forbidden[0]); ++index) {
        if(strstr(summary, forbidden[index])) return false;
    }
    PoisonDiagnosticEvent* event = &diagnostics->events[diagnostics->next_event];
    memset(event, 0, sizeof(*event));
    event->event_id = diagnostics->next_event_id++;
    event->category = category;
    event->timestamp_ms = timestamp_ms;
    memcpy(event->summary, summary, strlen(summary) + 1u);
    memcpy(event->correlation_digest, correlation_digest, 32);
    diagnostics->next_event = (diagnostics->next_event + 1u) % POISON_DIAGNOSTICS_RING_SIZE;
    poison_diagnostics_increment(diagnostics, category);
    return true;
}

const PoisonDiagnosticCounters* poison_diagnostics_counters(const PoisonDiagnostics* diagnostics) {
    return diagnostics ? &diagnostics->counters : NULL;
}

const char* poison_diagnostics_category_name(PoisonDiagnosticCategory category) {
    static const char* const names[PoisonDiagnosticCategoryCount] = {
        "session-established",
        "transport-error",
        "dropped-frame",
        "retried-frame",
        "command-failure",
        "app-crash",
        "policy-denial",
        "package-verification",
        "package-revocation",
        "update-stage",
        "update-health",
        "update-rollback",
        "recovery",
        "javascript-start",
        "javascript-terminal",
        "javascript-crash",
        "javascript-limit",
        "javascript-recovery",
    };
    return category < PoisonDiagnosticCategoryCount ? names[category] : NULL;
}

void poison_diagnostics_on_system_start(void) {
    poison_diagnostics_init(&poison_diagnostics);
}

PoisonDiagnostics* poison_diagnostics_get(void) {
    return &poison_diagnostics;
}
