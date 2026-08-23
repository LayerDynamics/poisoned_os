#include <string.h>
#include <stdio.h>

#include "../../../../services/rpc/rpc_poison_channel.h"
#include "../test.h"

MU_TEST(poison_channel_enforces_credit_and_sequence) {
    PoisonChannelTable table;
    poison_channel_table_init(&table);
    size_t channel_index = 0;
    mu_check(poison_channel_open(&table, "device", 1, &channel_index) == PoisonChannelResultOk);

    uint64_t sequence = UINT64_MAX;
    mu_check(
        poison_channel_reserve_send(&table, channel_index, 32, &sequence) ==
        PoisonChannelResultOk);
    mu_check(sequence == 0);
    mu_check(
        poison_channel_reserve_send(&table, channel_index, 32, &sequence) ==
        PoisonChannelResultNoCredit);
    mu_check(poison_channel_receive(&table, channel_index, 32, 1) == PoisonChannelResultGap);
    mu_check(poison_channel_receive(&table, channel_index, 32, 0) == PoisonChannelResultOk);
    mu_check(poison_channel_receive(&table, channel_index, 32, 0) == PoisonChannelResultDuplicate);
}

MU_TEST(poison_channel_rejects_invalid_size_and_names) {
    PoisonChannelTable table;
    poison_channel_table_init(&table);
    size_t channel_index = 0;
    char long_name[POISON_CHANNEL_NAME_MAX + 2u] = {0};
    memset(long_name, 'x', sizeof(long_name) - 1u);
    mu_check(
        poison_channel_open(&table, long_name, 1, &channel_index) == PoisonChannelResultInvalid);
    mu_check(poison_channel_open(&table, "device", 1, &channel_index) == PoisonChannelResultOk);
    uint64_t sequence = 0;
    mu_check(
        poison_channel_reserve_send(
            &table, channel_index, POISON_CHANNEL_MAX_FRAME_BYTES + 1u, &sequence) ==
        PoisonChannelResultInvalid);
}

MU_TEST(poison_channel_bounds_fixed_table) {
    PoisonChannelTable table;
    poison_channel_table_init(&table);
    size_t channel_index = 0;
    for(size_t index = 0; index < POISON_CHANNEL_MAX_CHANNELS; ++index) {
        char name[POISON_CHANNEL_NAME_MAX + 1u];
        snprintf(name, sizeof(name), "ch%zu", index);
        mu_check(poison_channel_open(&table, name, 0, &channel_index) == PoisonChannelResultOk);
    }
    mu_check(
        poison_channel_open(&table, "overflow", 0, &channel_index) == PoisonChannelResultFull);
}

MU_TEST(poison_channel_restores_a_bounded_resume_sequence) {
    PoisonChannelTable table;
    poison_channel_table_init(&table);
    size_t channel_index = 0u;
    mu_check(
        poison_channel_open_at(&table, "rpc", 2u, 41u, &channel_index) == PoisonChannelResultOk);
    const PoisonChannel* channel = poison_channel_get(&table, channel_index);
    mu_check(channel);
    mu_check(channel->next_tx_sequence == 41u);
    mu_check(channel->next_rx_sequence == 41u);
    size_t found_index = POISON_CHANNEL_MAX_CHANNELS;
    mu_check(poison_channel_find(&table, "rpc", &found_index));
    mu_check(found_index == channel_index);
    mu_check(!poison_channel_find(&table, "missing", &found_index));
    mu_check(
        poison_channel_open_at(&table, "wrapped", 1u, UINT64_MAX, &channel_index) ==
        PoisonChannelResultInvalid);
}

MU_TEST(poison_channel_rpc_requires_authenticated_frame) {
    PoisonSession receiver;
    PoisonSession sender;
    uint8_t key[POISON_SESSION_KEY_BYTES];
    memset(key, 0x5A, sizeof(key));
    poison_session_init(&receiver);
    poison_session_init(&sender);
    for(PoisonSession* session = &receiver; session;
        session = session == &receiver ? &sender : NULL) {
        mu_check(poison_session_begin_negotiation(session, 2u) == PoisonSessionResultOk);
        mu_check(poison_session_begin_confirmation(session, 9u) == PoisonSessionResultOk);
        mu_check(poison_session_set_authentication_key(session, key) == PoisonSessionResultOk);
        mu_check(poison_session_confirm(session, true) == PoisonSessionResultOk);
        mu_check(poison_session_activate(session) == PoisonSessionResultOk);
    }
    PoisonChannelTable table;
    poison_channel_table_init(&table);
    size_t channel_index = 0;
    mu_check(poison_channel_open(&table, "device", 1u, &channel_index) == PoisonChannelResultOk);
    const uint8_t payload[] = {0x42};
    uint8_t tag[POISON_SESSION_AUTH_TAG_BYTES];
    mu_check(
        poison_session_sign_frame(&sender, 2u, 0u, 0u, "device", payload, sizeof(payload), tag) ==
        PoisonSessionResultOk);
    mu_check(rpc_poison_channel_receive_authenticated(
        &receiver, 2u, 0u, 0u, "device", payload, sizeof(payload), tag, &table, channel_index, 0u));
    mu_check(!rpc_poison_channel_receive_authenticated(
        &receiver, 2u, 1u, 0u, "device", payload, sizeof(payload), tag, &table, channel_index, 1u));
}

MU_TEST_SUITE(poison_channel_suite) {
    MU_RUN_TEST(poison_channel_enforces_credit_and_sequence);
    MU_RUN_TEST(poison_channel_rejects_invalid_size_and_names);
    MU_RUN_TEST(poison_channel_bounds_fixed_table);
    MU_RUN_TEST(poison_channel_restores_a_bounded_resume_sequence);
    MU_RUN_TEST(poison_channel_rpc_requires_authenticated_frame);
}

void poison_channel_run_tests(void) {
    MU_RUN_SUITE(poison_channel_suite);
}
