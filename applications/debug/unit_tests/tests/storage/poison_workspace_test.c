#include "../test.h"
#include "../../../../services/poison_vfs/poison_workspace.h"
#include "../../../../services/rpc/rpc_poison_workspace.h"

#include <string.h>

static void activate_pair(PoisonSession* receiver, PoisonSession* sender) {
    uint8_t key[POISON_SESSION_KEY_BYTES];
    memset(key, 0x37, sizeof(key));
    poison_session_init(receiver);
    poison_session_init(sender);
    for(PoisonSession* session = receiver; session;
        session = session == receiver ? sender : NULL) {
        poison_session_begin_negotiation(session, 2u);
        poison_session_begin_confirmation(session, 7u);
        poison_session_set_authentication_key(session, key);
        poison_session_confirm(session, true);
        poison_session_activate(session);
    }
}

MU_TEST(poison_workspace_reset_is_exact_and_isolated) {
    PoisonWorkspaceSnapshot snapshot = {0};
    mu_check(poison_workspace_snapshot_create(&snapshot, "snap-1", "/cases/case-1"));
    mu_check(poison_workspace_reset_preview(&snapshot, "/cases/case-1"));
    mu_check(!poison_workspace_reset_preview(&snapshot, "/cases/case-2"));
    mu_check(!poison_workspace_reset_confirm(&snapshot, "/cases/case-1", false));
    mu_check(poison_workspace_reset_confirm(&snapshot, "/cases/case-1", true));
    mu_check(!snapshot.active);
}

MU_TEST(poison_workspace_rpc_requires_authenticated_exact_confirmation) {
    PoisonSession receiver, sender;
    PoisonWorkspaceSnapshot snapshot = {0};
    const uint8_t payload[] = {0x01, 0x02};
    uint8_t tag[POISON_SESSION_AUTH_TAG_BYTES];
    activate_pair(&receiver, &sender);
    mu_check(
        poison_session_sign_frame(
            &sender, 2u, 0u, 0u, "workspace", payload, sizeof(payload), tag) ==
        PoisonSessionResultOk);
    mu_check(rpc_poison_workspace_snapshot_create_authenticated(
        &receiver,
        2u,
        0u,
        0u,
        "workspace",
        payload,
        sizeof(payload),
        tag,
        &snapshot,
        "snap-rpc",
        "/cases/case-rpc"));
    mu_check(
        poison_session_sign_frame(
            &sender, 2u, 1u, 0u, "workspace", payload, sizeof(payload), tag) ==
        PoisonSessionResultOk);
    mu_check(!rpc_poison_workspace_reset_confirm_authenticated(
        &receiver,
        2u,
        1u,
        0u,
        "workspace",
        payload,
        sizeof(payload),
        tag,
        &snapshot,
        "/cases/other",
        true));
    mu_check(rpc_poison_workspace_reset_confirm_authenticated(
        &receiver,
        2u,
        1u,
        0u,
        "workspace",
        payload,
        sizeof(payload),
        tag,
        &snapshot,
        "/cases/case-rpc",
        true));
}

MU_TEST_SUITE(poison_workspace_suite) {
    MU_RUN_TEST(poison_workspace_reset_is_exact_and_isolated);
    MU_RUN_TEST(poison_workspace_rpc_requires_authenticated_exact_confirmation);
}
void poison_workspace_run_tests(void) {
    MU_RUN_SUITE(poison_workspace_suite);
}
