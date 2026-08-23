#include <string.h>

#include "../../../../services/poison_audit/poison_audit.h"
#include "../../../../services/poison_audit/poison_audit_i.h"
#include "../../../../services/rpc/poison_confirmation.h"
#include "../test.h"

static void fill_digest(uint8_t digest[32], uint8_t value) {
    memset(digest, value, 32);
}

MU_TEST(poison_confirmation_binds_all_authorization_inputs) {
    PoisonConfirmation confirmation;
    uint8_t command[32], target[32], consequence[32];
    fill_digest(command, 1);
    fill_digest(target, 2);
    fill_digest(consequence, 3);
    mu_check(
        poison_confirmation_issue(
            &confirmation, 9, PoisonRoleOwner, command, target, consequence, 4, 100, 1000, true) ==
        PoisonConfirmationResultOk);
    mu_check(
        poison_confirmation_approve(
            &confirmation,
            9,
            PoisonRoleOwner,
            command,
            target,
            consequence,
            confirmation.token,
            4,
            200,
            true) == PoisonConfirmationResultOk);
    mu_check(poison_confirmation_is_authorized(&confirmation));
    mu_check(
        poison_confirmation_approve(
            &confirmation,
            9,
            PoisonRoleOwner,
            command,
            target,
            consequence,
            confirmation.token,
            4,
            201,
            true) == PoisonConfirmationResultReplay);
}

MU_TEST(poison_confirmation_rejects_changed_target_and_expiry) {
    PoisonConfirmation confirmation;
    uint8_t command[32], target[32], consequence[32], changed_target[32];
    fill_digest(command, 1);
    fill_digest(target, 2);
    fill_digest(changed_target, 4);
    fill_digest(consequence, 3);
    mu_check(
        poison_confirmation_issue(
            &confirmation, 9, PoisonRoleOperator, command, target, consequence, 4, 100, 10, false) ==
        PoisonConfirmationResultOk);
    mu_check(
        poison_confirmation_approve(
            &confirmation,
            9,
            PoisonRoleOperator,
            command,
            changed_target,
            consequence,
            confirmation.token,
            4,
            101,
            false) == PoisonConfirmationResultMismatch);
    mu_check(
        poison_confirmation_approve(
            &confirmation,
            9,
            PoisonRoleOperator,
            command,
            target,
            consequence,
            confirmation.token,
            4,
            110,
            false) == PoisonConfirmationResultExpired);
}

MU_TEST(poison_confirmation_rejects_changed_token) {
    PoisonConfirmation confirmation;
    uint8_t command[32], target[32], consequence[32], changed_token[16];
    fill_digest(command, 1);
    fill_digest(target, 2);
    fill_digest(consequence, 3);
    mu_check(
        poison_confirmation_issue(
            &confirmation, 9, PoisonRoleOwner, command, target, consequence, 4, 100, 1000, true) ==
        PoisonConfirmationResultOk);
    memcpy(changed_token, confirmation.token, sizeof(changed_token));
    changed_token[0] ^= 1u;
    mu_check(
        poison_confirmation_approve(
            &confirmation,
            9,
            PoisonRoleOwner,
            command,
            target,
            consequence,
            changed_token,
            4,
            200,
            true) == PoisonConfirmationResultMismatch);
    mu_check(!poison_confirmation_is_authorized(&confirmation));
}

MU_TEST(poison_audit_chain_redacts_and_links_events) {
    PoisonAuditChain chain;
    PoisonAuditEvent first, second;
    PoisonAuditEvent snapshot[POISON_AUDIT_RING_SIZE];
    uint8_t actor[32], correlation[32];
    fill_digest(actor, 1);
    fill_digest(correlation, 2);
    poison_audit_init(&chain);
    mu_check(poison_audit_append(
        &chain,
        actor,
        "pair",
        "client",
        PoisonAuditDecisionAllowed,
        100,
        correlation,
        "role=owner",
        &first));
    mu_check(poison_audit_append(
        &chain,
        actor,
        "revoke",
        "client",
        PoisonAuditDecisionRevoked,
        101,
        correlation,
        "reason=user",
        &second));
    mu_check(second.event_id == first.event_id + 1);
    mu_check(memcmp(second.prior_digest, first.digest, 32) == 0);
    bool truncated = false;
    uint64_t next_event_id = 0u;
    uint8_t last_digest[32];
    mu_check(
        poison_audit_snapshot(
            &chain, 0u, snapshot, POISON_AUDIT_RING_SIZE, &truncated, &next_event_id, last_digest) ==
        2u);
    mu_check(!truncated);
    mu_check(next_event_id == 3u);
    mu_check(snapshot[0].event_id == 1u && snapshot[1].event_id == 2u);
    mu_check(memcmp(last_digest, second.digest, sizeof(last_digest)) == 0);
    for(size_t index = 0u; index < POISON_AUDIT_RING_SIZE + 1u; ++index) {
        mu_check(poison_audit_append(
            &chain,
            actor,
            "status",
            "device",
            PoisonAuditDecisionAllowed,
            200u + index,
            correlation,
            "source=rpc",
            &second));
    }
    mu_check(
        poison_audit_snapshot(
            &chain, 0u, snapshot, POISON_AUDIT_RING_SIZE, &truncated, &next_event_id, last_digest) ==
        POISON_AUDIT_RING_SIZE);
    mu_check(truncated);
    mu_check(snapshot[0].event_id == 4u);
    mu_check(snapshot[POISON_AUDIT_RING_SIZE - 1u].event_id == 19u);
    mu_check(!poison_audit_append(
        &chain,
        actor,
        "export",
        "evidence",
        PoisonAuditDecisionAllowed,
        102,
        correlation,
        "secret=bad",
        &second));
}

MU_TEST(poison_confirmation_caller_inventory_is_complete) {
    for(PoisonConfirmationCaller caller = PoisonConfirmationCallerStorage;
        caller < PoisonConfirmationCallerCount;
        ++caller) {
        mu_check(poison_confirmation_caller_requires_confirmation(caller));
        mu_check(poison_confirmation_caller_name(caller) != NULL);
    }
    mu_check(!poison_confirmation_caller_requires_confirmation(PoisonConfirmationCallerCount));
}

MU_TEST_SUITE(poison_confirmation_suite) {
    MU_RUN_TEST(poison_confirmation_binds_all_authorization_inputs);
    MU_RUN_TEST(poison_confirmation_rejects_changed_target_and_expiry);
    MU_RUN_TEST(poison_confirmation_rejects_changed_token);
    MU_RUN_TEST(poison_audit_chain_redacts_and_links_events);
    MU_RUN_TEST(poison_confirmation_caller_inventory_is_complete);
}

void poison_confirmation_run_tests(void) {
    MU_RUN_SUITE(poison_confirmation_suite);
}
