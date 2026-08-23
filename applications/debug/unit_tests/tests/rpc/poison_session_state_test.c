#include "../../../../services/rpc/rpc_poison_session.h"
#include "../test.h"

MU_TEST(poison_session_requires_physical_confirmation) {
    PoisonSession session;
    poison_session_init(&session);
    mu_check(session.state == PoisonSessionUnpaired);
    mu_check(poison_session_begin_negotiation(&session, 2) == PoisonSessionResultOk);
    mu_check(poison_session_begin_confirmation(&session, 7) == PoisonSessionResultOk);
    mu_check(poison_session_activate(&session) == PoisonSessionResultState);
    mu_check(poison_session_confirm(&session, false) == PoisonSessionResultInvalid);
    mu_check(poison_session_confirm(&session, true) == PoisonSessionResultOk);
    mu_check(poison_session_activate(&session) == PoisonSessionResultOk);
}

MU_TEST(poison_session_rejects_replay_gap_and_revocation) {
    PoisonSession session;
    poison_session_init(&session);
    mu_check(poison_session_begin_negotiation(&session, 2) == PoisonSessionResultOk);
    mu_check(poison_session_begin_confirmation(&session, 1) == PoisonSessionResultOk);
    mu_check(poison_session_confirm(&session, true) == PoisonSessionResultOk);
    mu_check(poison_session_activate(&session) == PoisonSessionResultOk);

    uint64_t sequence = UINT64_MAX;
    mu_check(poison_session_reserve_tx(&session, &sequence) == PoisonSessionResultOk);
    mu_check(sequence == 0);
    mu_check(poison_session_accept_rx(&session, 1) == PoisonSessionResultGap);
    mu_check(poison_session_accept_rx(&session, 0) == PoisonSessionResultOk);
    mu_check(poison_session_accept_rx(&session, 0) == PoisonSessionResultReplay);
    mu_check(poison_session_revoke(&session) == PoisonSessionResultOk);
    mu_check(poison_session_reserve_tx(&session, &sequence) == PoisonSessionResultState);
}

MU_TEST_SUITE(poison_session_state_suite) {
    MU_RUN_TEST(poison_session_requires_physical_confirmation);
    MU_RUN_TEST(poison_session_rejects_replay_gap_and_revocation);
}

void poison_session_state_run_tests(void) {
    MU_RUN_SUITE(poison_session_state_suite);
}
