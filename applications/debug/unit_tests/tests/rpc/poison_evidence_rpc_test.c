#include "../test.h"
#include "../../../../services/rpc/rpc_poison_evidence.h"

static void active_session_pair(PoisonSession* receiver, PoisonSession* sender) {
    uint8_t key[POISON_SESSION_KEY_BYTES];
    for(size_t index = 0; index < sizeof(key); ++index)
        key[index] = (uint8_t)(index + 1u);
    poison_session_init(receiver);
    poison_session_begin_negotiation(receiver, 2u);
    poison_session_begin_confirmation(receiver, 9u);
    poison_session_set_authentication_key(receiver, key);
    poison_session_confirm(receiver, true);
    poison_session_activate(receiver);
    poison_session_init(sender);
    poison_session_begin_negotiation(sender, 2u);
    poison_session_begin_confirmation(sender, 9u);
    poison_session_set_authentication_key(sender, key);
    poison_session_confirm(sender, true);
    poison_session_activate(sender);
}

MU_TEST(poison_evidence_rpc_requires_authenticated_payload) {
    PoisonSession receiver, sender;
    PoisonEvidenceStore store;
    uint8_t previous[32] = {0};
    const uint8_t payload[] = "capture";
    uint8_t tag[POISON_SESSION_AUTH_TAG_BYTES] = {0};
    active_session_pair(&receiver, &sender);
    poison_evidence_store_init(&store);
    mu_check(
        poison_session_sign_frame(
            &sender, 2u, 0u, 0u, "evidence", payload, sizeof(payload) - 1u, tag) ==
        PoisonSessionResultOk);
    mu_check(rpc_poison_evidence_capture_authenticated(
        &receiver,
        2u,
        0u,
        0u,
        "evidence",
        payload,
        sizeof(payload) - 1u,
        tag,
        &store,
        "ev-rpc",
        "case-1",
        false,
        previous));
    mu_check(poison_evidence_find(&store, "ev-rpc") != NULL);
    mu_check(!rpc_poison_evidence_capture_authenticated(
        &receiver,
        2u,
        1u,
        0u,
        "evidence",
        payload,
        sizeof(payload) - 1u,
        tag,
        &store,
        "ev-other",
        "case-1",
        false,
        previous));
}

MU_TEST_SUITE(poison_evidence_rpc_suite) {
    MU_RUN_TEST(poison_evidence_rpc_requires_authenticated_payload);
}
void poison_evidence_rpc_run_tests(void) {
    MU_RUN_SUITE(poison_evidence_rpc_suite);
}
