#include <string.h>

#include "../../../../services/poison_diagnostics/poison_diagnostics.h"
#include "../../../../services/rpc/rpc_poison_diagnostics.h"
#include "../test.h"

MU_TEST(poison_diagnostics_saturates_and_redacts) {
    PoisonDiagnostics diagnostics;
    uint8_t correlation[32];
    memset(correlation, 1, sizeof(correlation));
    poison_diagnostics_init(&diagnostics);
    for(size_t index = 0; index < POISON_DIAGNOSTICS_RING_SIZE + 2u; ++index) {
        mu_check(poison_diagnostics_record(
            &diagnostics, PoisonDiagnosticTransportError, "timeout", index, correlation));
    }
    mu_check(
        poison_diagnostics_counters(&diagnostics)->transport_errors ==
        POISON_DIAGNOSTICS_RING_SIZE + 2u);
    mu_check(!poison_diagnostics_record(
        &diagnostics, PoisonDiagnosticTransportError, "secret leaked", 1, correlation));
    mu_check(poison_diagnostics_record(
        &diagnostics, PoisonDiagnosticPackageVerification, "archive accepted", 20u, correlation));
    mu_check(poison_diagnostics_record(
        &diagnostics, PoisonDiagnosticPackageRevocation, "signer revoked", 21u, correlation));
    mu_check(poison_diagnostics_record(
        &diagnostics, PoisonDiagnosticUpdateStage, "update staged", 22u, correlation));
    mu_check(poison_diagnostics_record(
        &diagnostics, PoisonDiagnosticUpdateHealth, "health accepted", 23u, correlation));
    mu_check(poison_diagnostics_record(
        &diagnostics, PoisonDiagnosticUpdateRollback, "update rolled back", 24u, correlation));
    mu_check(poison_diagnostics_record(
        &diagnostics, PoisonDiagnosticRecovery, "recovery completed", 25u, correlation));
    mu_check(poison_diagnostics_record(
        &diagnostics, PoisonDiagnosticJavascriptStart, "javascript started", 26u, correlation));
    mu_check(poison_diagnostics_record(
        &diagnostics,
        PoisonDiagnosticJavascriptTerminal,
        "javascript completed",
        27u,
        correlation));
    mu_check(poison_diagnostics_record(
        &diagnostics, PoisonDiagnosticJavascriptCrash, "javascript crashed", 28u, correlation));
    mu_check(poison_diagnostics_record(
        &diagnostics, PoisonDiagnosticJavascriptLimit, "javascript fuel limit", 29u, correlation));
    mu_check(poison_diagnostics_record(
        &diagnostics,
        PoisonDiagnosticJavascriptRecovery,
        "javascript recovery verified",
        30u,
        correlation));
    const PoisonDiagnosticCounters* counters = poison_diagnostics_counters(&diagnostics);
    mu_check(counters->package_verifications == 1u);
    mu_check(counters->package_revocations == 1u);
    mu_check(counters->update_stages == 1u);
    mu_check(counters->update_health == 1u);
    mu_check(counters->update_rollbacks == 1u);
    mu_check(counters->recoveries == 1u);
    mu_check(counters->javascript_starts == 1u);
    mu_check(counters->javascript_terminals == 1u);
    mu_check(counters->javascript_crashes == 1u);
    mu_check(counters->javascript_limits == 1u);
    mu_check(counters->javascript_recoveries == 1u);
    mu_check(
        strcmp(
            poison_diagnostics_category_name(PoisonDiagnosticUpdateRollback), "update-rollback") ==
        0);
}

MU_TEST(poison_diagnostics_rpc_returns_authenticated_ordered_snapshot) {
    PoisonSession receiver, sender;
    uint8_t key[POISON_SESSION_KEY_BYTES];
    uint8_t correlation[32] = {0};
    uint8_t tag[POISON_SESSION_AUTH_TAG_BYTES];
    const uint8_t payload[] = {0x01};
    memset(key, 0x19, sizeof(key));
    poison_session_init(&receiver);
    poison_session_init(&sender);
    for(PoisonSession* session = &receiver; session;
        session = session == &receiver ? &sender : NULL) {
        poison_session_begin_negotiation(session, 2u);
        poison_session_begin_confirmation(session, 21u);
        poison_session_set_authentication_key(session, key);
        poison_session_confirm(session, true);
        poison_session_activate(session);
    }
    PoisonDiagnostics diagnostics;
    poison_diagnostics_init(&diagnostics);
    mu_check(poison_diagnostics_record(
        &diagnostics, PoisonDiagnosticTransportError, "timeout", 7u, correlation));
    mu_check(
        poison_session_sign_frame(
            &sender, 2u, 0u, 0u, "diagnostics", payload, sizeof(payload), tag) ==
        PoisonSessionResultOk);
    PoisonDiagnosticCounters counters = {0};
    PoisonDiagnosticEvent events[POISON_DIAGNOSTICS_RING_SIZE] = {0};
    size_t event_count = 0;
    mu_check(rpc_poison_diagnostics_snapshot_authenticated(
        &receiver,
        2u,
        0u,
        0u,
        "diagnostics",
        payload,
        sizeof(payload),
        tag,
        &diagnostics,
        &counters,
        events,
        POISON_DIAGNOSTICS_RING_SIZE,
        &event_count));
    mu_check(event_count == 1u);
    mu_check(counters.transport_errors == 1u);
    mu_check(events[0].event_id == 1u);
    mu_check(!rpc_poison_diagnostics_snapshot_authenticated(
        &receiver,
        2u,
        1u,
        0u,
        "wrong",
        payload,
        sizeof(payload),
        tag,
        &diagnostics,
        &counters,
        events,
        POISON_DIAGNOSTICS_RING_SIZE,
        &event_count));
}

MU_TEST_SUITE(poison_diagnostics_suite) {
    MU_RUN_TEST(poison_diagnostics_saturates_and_redacts);
    MU_RUN_TEST(poison_diagnostics_rpc_returns_authenticated_ordered_snapshot);
}

void poison_diagnostics_run_tests(void) {
    MU_RUN_SUITE(poison_diagnostics_suite);
}
