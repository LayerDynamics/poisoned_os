#pragma once

#include "rpc.h"
#include "rpc_poison_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_CHANNEL_MAX_CHANNELS    (4u)
#define POISON_CHANNEL_NAME_MAX        (32u)
#define POISON_CHANNEL_MAX_CREDITS     (4u)
#define POISON_CHANNEL_MAX_FRAME_BYTES RPC_BUFFER_SIZE

typedef enum {
    PoisonChannelResultOk,
    PoisonChannelResultInvalid,
    PoisonChannelResultFull,
    PoisonChannelResultClosed,
    PoisonChannelResultDuplicate,
    PoisonChannelResultGap,
    PoisonChannelResultNoCredit,
    PoisonChannelResultSequenceWrap,
} PoisonChannelResult;

typedef struct {
    bool active;
    char name[POISON_CHANNEL_NAME_MAX + 1u];
    uint64_t next_tx_sequence;
    uint64_t next_rx_sequence;
    uint32_t credits;
} PoisonChannel;

typedef struct {
    PoisonChannel channels[POISON_CHANNEL_MAX_CHANNELS];
} PoisonChannelTable;

void poison_channel_table_init(PoisonChannelTable* table);

PoisonChannelResult poison_channel_open(
    PoisonChannelTable* table,
    const char* name,
    uint32_t initial_credits,
    size_t* channel_index);

PoisonChannelResult poison_channel_open_at(
    PoisonChannelTable* table,
    const char* name,
    uint32_t initial_credits,
    uint64_t next_sequence,
    size_t* channel_index);

PoisonChannelResult poison_channel_close(PoisonChannelTable* table, size_t channel_index);

PoisonChannelResult poison_channel_reserve_send(
    PoisonChannelTable* table,
    size_t channel_index,
    size_t frame_bytes,
    uint64_t* sequence);

PoisonChannelResult poison_channel_receive(
    PoisonChannelTable* table,
    size_t channel_index,
    size_t frame_bytes,
    uint64_t sequence);

PoisonChannelResult
    poison_channel_add_credits(PoisonChannelTable* table, size_t channel_index, uint32_t credits);

const PoisonChannel* poison_channel_get(const PoisonChannelTable* table, size_t channel_index);

bool poison_channel_find(const PoisonChannelTable* table, const char* name, size_t* channel_index);

bool rpc_poison_channel_receive_authenticated(
    PoisonSession* session,
    uint32_t protocol_version,
    uint64_t sequence,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* payload,
    size_t payload_length,
    const uint8_t authentication_tag[POISON_SESSION_AUTH_TAG_BYTES],
    PoisonChannelTable* table,
    size_t channel_index,
    uint64_t channel_sequence);

#ifdef __cplusplus
}
#endif
