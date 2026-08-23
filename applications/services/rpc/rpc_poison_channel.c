#include "rpc_poison_channel.h"
#include "rpc_i.h"

#include <limits.h>
#include <string.h>

static bool poison_channel_index_valid(size_t channel_index) {
    return channel_index < POISON_CHANNEL_MAX_CHANNELS;
}

static PoisonChannel* poison_channel_at(PoisonChannelTable* table, size_t channel_index) {
    if(!table || !poison_channel_index_valid(channel_index)) return NULL;
    return &table->channels[channel_index];
}

static const PoisonChannel*
    poison_channel_at_const(const PoisonChannelTable* table, size_t channel_index) {
    if(!table || !poison_channel_index_valid(channel_index)) return NULL;
    return &table->channels[channel_index];
}

void poison_channel_table_init(PoisonChannelTable* table) {
    if(!table) return;
    memset(table, 0, sizeof(*table));
}

PoisonChannelResult poison_channel_open(
    PoisonChannelTable* table,
    const char* name,
    uint32_t initial_credits,
    size_t* channel_index) {
    return poison_channel_open_at(table, name, initial_credits, 0u, channel_index);
}

PoisonChannelResult poison_channel_open_at(
    PoisonChannelTable* table,
    const char* name,
    uint32_t initial_credits,
    uint64_t next_sequence,
    size_t* channel_index) {
    if(!table || !name || !channel_index || name[0] == '\0' || next_sequence == UINT64_MAX)
        return PoisonChannelResultInvalid;
    size_t name_length = strnlen(name, POISON_CHANNEL_NAME_MAX + 1u);
    if(name_length == 0 || name_length > POISON_CHANNEL_NAME_MAX)
        return PoisonChannelResultInvalid;
    if(initial_credits > POISON_CHANNEL_MAX_CREDITS) return PoisonChannelResultInvalid;

    for(size_t index = 0; index < POISON_CHANNEL_MAX_CHANNELS; ++index) {
        PoisonChannel* channel = &table->channels[index];
        if(channel->active && strcmp(channel->name, name) == 0) return PoisonChannelResultInvalid;
    }
    for(size_t index = 0; index < POISON_CHANNEL_MAX_CHANNELS; ++index) {
        PoisonChannel* channel = &table->channels[index];
        if(!channel->active) {
            memcpy(channel->name, name, name_length + 1u);
            channel->active = true;
            channel->next_tx_sequence = next_sequence;
            channel->next_rx_sequence = next_sequence;
            channel->credits = initial_credits;
            *channel_index = index;
            return PoisonChannelResultOk;
        }
    }
    return PoisonChannelResultFull;
}

PoisonChannelResult poison_channel_close(PoisonChannelTable* table, size_t channel_index) {
    PoisonChannel* channel = poison_channel_at(table, channel_index);
    if(!channel) return PoisonChannelResultInvalid;
    if(!channel->active) return PoisonChannelResultClosed;
    memset(channel, 0, sizeof(*channel));
    return PoisonChannelResultOk;
}

PoisonChannelResult poison_channel_reserve_send(
    PoisonChannelTable* table,
    size_t channel_index,
    size_t frame_bytes,
    uint64_t* sequence) {
    PoisonChannel* channel = poison_channel_at(table, channel_index);
    if(!channel || !sequence) return PoisonChannelResultInvalid;
    if(!channel->active) return PoisonChannelResultClosed;
    if(frame_bytes > POISON_CHANNEL_MAX_FRAME_BYTES) return PoisonChannelResultInvalid;
    if(channel->credits == 0) return PoisonChannelResultNoCredit;
    if(channel->next_tx_sequence == UINT64_MAX) return PoisonChannelResultSequenceWrap;
    *sequence = channel->next_tx_sequence++;
    --channel->credits;
    return PoisonChannelResultOk;
}

PoisonChannelResult poison_channel_receive(
    PoisonChannelTable* table,
    size_t channel_index,
    size_t frame_bytes,
    uint64_t sequence) {
    PoisonChannel* channel = poison_channel_at(table, channel_index);
    if(!channel) return PoisonChannelResultInvalid;
    if(!channel->active) return PoisonChannelResultClosed;
    if(frame_bytes > POISON_CHANNEL_MAX_FRAME_BYTES) return PoisonChannelResultInvalid;
    if(sequence < channel->next_rx_sequence) return PoisonChannelResultDuplicate;
    if(sequence > channel->next_rx_sequence) return PoisonChannelResultGap;
    if(channel->next_rx_sequence == UINT64_MAX) return PoisonChannelResultSequenceWrap;
    ++channel->next_rx_sequence;
    return PoisonChannelResultOk;
}

PoisonChannelResult
    poison_channel_add_credits(PoisonChannelTable* table, size_t channel_index, uint32_t credits) {
    PoisonChannel* channel = poison_channel_at(table, channel_index);
    if(!channel) return PoisonChannelResultInvalid;
    if(!channel->active) return PoisonChannelResultClosed;
    if(credits > POISON_CHANNEL_MAX_CREDITS - channel->credits) return PoisonChannelResultInvalid;
    channel->credits += credits;
    return PoisonChannelResultOk;
}

const PoisonChannel* poison_channel_get(const PoisonChannelTable* table, size_t channel_index) {
    const PoisonChannel* channel = poison_channel_at_const(table, channel_index);
    if(!channel || !channel->active) return NULL;
    return channel;
}

bool poison_channel_find(const PoisonChannelTable* table, const char* name, size_t* channel_index) {
    if(!table || !name || !channel_index) return false;
    for(size_t index = 0u; index < POISON_CHANNEL_MAX_CHANNELS; ++index) {
        const PoisonChannel* channel = poison_channel_get(table, index);
        if(channel && strcmp(channel->name, name) == 0) {
            *channel_index = index;
            return true;
        }
    }
    return false;
}

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
    uint64_t channel_sequence) {
    const PoisonChannel* target = poison_channel_get(table, channel_index);
    if(!target || !channel || strcmp(target->name, channel) != 0) return false;
    if(poison_session_authenticate_rx(
           session,
           protocol_version,
           sequence,
           acknowledgement,
           channel,
           payload,
           payload_length,
           authentication_tag) != PoisonSessionResultOk) {
        return false;
    }
    return poison_channel_receive(table, channel_index, payload_length, channel_sequence) ==
           PoisonChannelResultOk;
}

typedef struct {
    RpcSession* session;
} RpcPoisonChannel;

static void rpc_poison_channel_open_process(const PB_Main* request, void* context) {
    RpcPoisonChannel* system = context;
    furi_assert(system);
    PB_Main response = PB_Main_init_zero;
    response.command_id = request->command_id;
    if(request->which_content != PB_Main_poison_channel_open_tag || request->has_next ||
       !rpc_session_is_secure_dispatch_active(system->session)) {
        response.command_status = PB_CommandStatus_ERROR_INVALID_PARAMETERS;
        response.which_content = PB_Main_empty_tag;
        rpc_send_and_release(system->session, &response);
        return;
    }
    const PB_Poison_ChannelOpen* input = &request->content.poison_channel_open;
    uint32_t granted_credits = 0u;
    uint64_t granted_sequence = 0u;
    const bool valid = strcmp(input->channel, "rpc") == 0 && input->initial_credits > 0u &&
                       rpc_session_open_secure_channel(
                           system->session,
                           input->channel,
                           input->initial_credits,
                           input->resume_sequence,
                           &granted_credits,
                           &granted_sequence);
    if(!valid) {
        response.command_status = PB_CommandStatus_ERROR_INVALID_PARAMETERS;
        response.which_content = PB_Main_empty_tag;
        rpc_send_and_release(system->session, &response);
        return;
    }
    response.command_status = PB_CommandStatus_OK;
    response.which_content = PB_Main_poison_channel_opened_tag;
    strcpy(response.content.poison_channel_opened.channel, input->channel);
    response.content.poison_channel_opened.granted_credits = granted_credits;
    response.content.poison_channel_opened.next_sequence = granted_sequence;
    rpc_send_and_release(system->session, &response);
}

static void rpc_poison_credit_update_process(const PB_Main* request, void* context) {
    RpcPoisonChannel* system = context;
    furi_assert(system);
    bool accepted = request->which_content == PB_Main_poison_credit_update_tag &&
                    !request->has_next && rpc_session_is_secure_dispatch_active(system->session);
    if(accepted) {
        const PB_Poison_CreditUpdate* input = &request->content.poison_credit_update;
        accepted = rpc_session_add_secure_channel_credits(
            system->session, input->channel, input->credits);
    }
    rpc_send_and_release_empty(
        system->session,
        request->command_id,
        accepted ? PB_CommandStatus_OK : PB_CommandStatus_ERROR_INVALID_PARAMETERS);
}

void* rpc_system_poison_channel_alloc(RpcSession* session) {
    RpcPoisonChannel* system = malloc(sizeof(*system));
    furi_check(system);
    memset(system, 0, sizeof(*system));
    system->session = session;
    RpcHandler handler = {
        .message_handler = rpc_poison_channel_open_process,
        .decode_submessage = NULL,
        .context = system,
    };
    rpc_add_handler(session, PB_Main_poison_channel_open_tag, &handler);
    handler.message_handler = rpc_poison_credit_update_process;
    rpc_add_handler(session, PB_Main_poison_credit_update_tag, &handler);
    return system;
}

void rpc_system_poison_channel_free(void* context) {
    if(!context) return;
    RpcPoisonChannel* system = context;
    memset(system, 0, sizeof(*system));
    free(system);
}
