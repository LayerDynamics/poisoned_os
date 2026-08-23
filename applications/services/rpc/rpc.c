#include "rpc_i.h"
#include "../poison_packages/poison_content_update_internal.h"
#include "../poison_diagnostics/poison_diagnostics.h"
#include "poison_pairing_store.h"

#include <pb.h>
#include <pb_decode.h>
#include <pb_encode.h>

#include <storage.pb.h>
#include <flipper.pb.h>

#include <furi.h>
#include <furi_hal.h>

#include <toolbox/cli/cli_command.h>
#include <cli/cli_main_commands.h>
#include <stdint.h>
#include <stdio.h>
#include <m-dict.h>
#include <mbedtls/sha256.h>

#include <bt/bt_service/bt.h>
#include <dialogs/dialogs.h>
#include <update_util/update_operation.h>

#define TAG                            "RpcSrv"
#define RPC_SECURE_REQUEST_PAYLOAD_MAX (768u)
#define RPC_RESUME_SLOT_COUNT          (8u)
#define RPC_RESUME_TTL_MS              (60000u)
#define RPC_CANCELLATION_SLOT_COUNT    (4u)

typedef enum {
    RpcEvtNewData = (1 << 0),
    RpcEvtDisconnect = (1 << 1),
} RpcEvtFlags;

#define RPC_ALL_EVENTS (RpcEvtNewData | RpcEvtDisconnect)

DICT_DEF2(RpcHandlerDict, pb_size_t, M_DEFAULT_OPLIST, RpcHandler, M_POD_OPLIST)

typedef struct {
    RpcSystemAlloc alloc;
    RpcSystemFree free;
    void* context;
} RpcSystemCallbacks;

typedef struct {
    bool active;
    uint32_t command_id;
    RpcCancellationCallback callback;
    void* context;
} RpcCancellationSlot;

static RpcSystemCallbacks rpc_systems[] = {
    {
        .alloc = rpc_system_system_alloc,
        .free = NULL,
    },
    {
        .alloc = rpc_system_storage_alloc,
        .free = rpc_system_storage_free,
    },
    {
        .alloc = rpc_system_app_alloc,
        .free = rpc_system_app_free,
    },
    {
        .alloc = rpc_system_gui_alloc,
        .free = rpc_system_gui_free,
    },
    {
        .alloc = rpc_system_gpio_alloc,
        .free = NULL,
    },
    {
        .alloc = rpc_system_property_alloc,
        .free = NULL,
    },
    {
        .alloc = rpc_desktop_alloc,
        .free = rpc_desktop_free,
    },
    {
        .alloc = rpc_system_poison_alloc,
        .free = rpc_system_poison_free,
    },
    {
        .alloc = rpc_system_poison_channel_alloc,
        .free = rpc_system_poison_channel_free,
    },
    {
        .alloc = rpc_system_poison_packages_alloc,
        .free = rpc_system_poison_packages_free,
    },
    {
        .alloc = rpc_system_poison_package_catalog_alloc,
        .free = rpc_system_poison_package_catalog_free,
    },
    {
        .alloc = rpc_system_poison_app_alloc,
        .free = rpc_system_poison_app_free,
    },
    {
        .alloc = rpc_system_poison_profiles_alloc,
        .free = rpc_system_poison_profiles_free,
    },
    {
        .alloc = rpc_system_poison_tools_alloc,
        .free = rpc_system_poison_tools_free,
    },
    {
        .alloc = rpc_system_poison_files_alloc,
        .free = rpc_system_poison_files_free,
    },
    {
        .alloc = rpc_system_poison_evidence_alloc,
        .free = rpc_system_poison_evidence_free,
    },
    {
        .alloc = rpc_system_poison_audit_alloc,
        .free = rpc_system_poison_audit_free,
    },
    {
        .alloc = rpc_system_poison_workload_alloc,
        .free = rpc_system_poison_workload_free,
    },
    {
        .alloc = rpc_system_poison_js_bundle_alloc,
        .free = rpc_system_poison_js_bundle_free,
    },
};

struct RpcSession {
    Rpc* rpc;

    FuriThread* thread;

    RpcHandlerDict_t handlers;
    FuriStreamBuffer* stream;
    PB_Main* decoded_message;
    bool terminate;
    void** system_contexts;
    bool decode_error;

    FuriMutex* callbacks_mutex;
    FuriMutex* secure_mutex;
    RpcSendBytesCallback send_bytes_callback;
    RpcBufferIsEmptyCallback buffer_is_empty_callback;
    RpcSessionClosedCallback closed_callback;
    RpcSessionTerminatedCallback terminated_callback;
    RpcPairingConfirmationCallback pairing_confirmation_callback;
    void* pairing_confirmation_context;
    RpcContentUpdateConfirmationCallback content_update_confirmation_callback;
    void* content_update_confirmation_context;
    RpcProfileConfirmationCallback profile_confirmation_callback;
    void* profile_confirmation_context;
    RpcOwner owner;
    void* context;
    bool secure_dispatch_active;
    bool secure_transport_active;
    PoisonSession* secure_session;
    char secure_channel[POISON_SESSION_CHANNEL_MAX + 1u];
    uint64_t secure_acknowledgement;
    PoisonChannelTable secure_channels;
    RpcCancellationSlot cancellations[RPC_CANCELLATION_SLOT_COUNT];
};

struct Rpc {
    FuriMutex* busy_mutex;
    FuriMutex* resume_mutex;
    PoisonSessionResumeSlot resume_slots[RPC_RESUME_SLOT_COUNT];
    RpcPoisonContentUpdate content_update;
};

RpcOwner rpc_session_get_owner(RpcSession* session) {
    furi_check(session);
    return session->owner;
}

RpcPoisonContentUpdate* rpc_session_content_update(RpcSession* session) {
    furi_check(session);
    furi_check(session->rpc);
    return &session->rpc->content_update;
}

bool rpc_session_store_resume(
    RpcSession* session,
    const PoisonSession* poison_session,
    const uint8_t token[POISON_SESSION_RESUME_TOKEN_BYTES]) {
    if(!session || !session->rpc || !poison_session || !token) return false;
    Rpc* rpc = session->rpc;
    const uint64_t now_ms = ((uint64_t)furi_get_tick() * 1000u) / furi_kernel_get_tick_frequency();
    furi_check(furi_mutex_acquire(rpc->resume_mutex, FuriWaitForever) == FuriStatusOk);
    size_t selected = RPC_RESUME_SLOT_COUNT;
    uint64_t oldest_expiry = UINT64_MAX;
    for(size_t index = 0u; index < RPC_RESUME_SLOT_COUNT; ++index) {
        PoisonSessionResumeSlot* slot = &rpc->resume_slots[index];
        if(slot->active && slot->session.session_id == poison_session->session_id) {
            selected = index;
            break;
        }
        if(!slot->active || now_ms >= slot->expires_at_ms) {
            selected = index;
            break;
        }
        if(slot->expires_at_ms < oldest_expiry) {
            oldest_expiry = slot->expires_at_ms;
            selected = index;
        }
    }
    bool stored =
        selected < RPC_RESUME_SLOT_COUNT &&
        poison_session_resume_store(
            &rpc->resume_slots[selected], poison_session, token, now_ms, RPC_RESUME_TTL_MS);
    furi_check(furi_mutex_release(rpc->resume_mutex) == FuriStatusOk);
    return stored;
}

bool rpc_session_take_resume(
    RpcSession* session,
    uint64_t session_id,
    const uint8_t token[POISON_SESSION_RESUME_TOKEN_BYTES],
    uint64_t last_received_sequence,
    PoisonSession* resumed_session) {
    if(!session || !session->rpc || !token || !resumed_session) return false;
    Rpc* rpc = session->rpc;
    const uint64_t now_ms = ((uint64_t)furi_get_tick() * 1000u) / furi_kernel_get_tick_frequency();
    furi_check(furi_mutex_acquire(rpc->resume_mutex, FuriWaitForever) == FuriStatusOk);
    bool resumed = false;
    for(size_t index = 0u; index < RPC_RESUME_SLOT_COUNT && !resumed; ++index) {
        PoisonSessionResumeSlot* slot = &rpc->resume_slots[index];
        if(slot->active && slot->session.session_id == session_id) {
            resumed = poison_session_resume_take(
                slot, session_id, token, last_received_sequence, now_ms, resumed_session);
        } else if(slot->active && now_ms >= slot->expires_at_ms) {
            poison_session_resume_revoke(slot);
        }
    }
    furi_check(furi_mutex_release(rpc->resume_mutex) == FuriStatusOk);
    return resumed;
}

void rpc_session_revoke_resume(RpcSession* session, uint64_t session_id) {
    if(!session || !session->rpc) return;
    Rpc* rpc = session->rpc;
    furi_check(furi_mutex_acquire(rpc->resume_mutex, FuriWaitForever) == FuriStatusOk);
    for(size_t index = 0u; index < RPC_RESUME_SLOT_COUNT; ++index) {
        PoisonSessionResumeSlot* slot = &rpc->resume_slots[index];
        if(slot->active && slot->session.session_id == session_id)
            poison_session_resume_revoke(slot);
    }
    furi_check(furi_mutex_release(rpc->resume_mutex) == FuriStatusOk);
}

void rpc_session_revoke_secure_transport(RpcSession* session) {
    if(!session) return;
    uint64_t session_id = 0u;
    furi_check(furi_mutex_acquire(session->secure_mutex, FuriWaitForever) == FuriStatusOk);
    if(session->secure_session) {
        session_id = session->secure_session->session_id;
        poison_session_revoke(session->secure_session);
    }
    session->secure_dispatch_active = false;
    session->secure_transport_active = false;
    session->secure_session = NULL;
    session->secure_channel[0] = '\0';
    session->secure_acknowledgement = 0u;
    poison_channel_table_init(&session->secure_channels);
    furi_check(furi_mutex_release(session->secure_mutex) == FuriStatusOk);
    if(session_id != 0u) rpc_session_revoke_resume(session, session_id);
}

bool rpc_session_open_secure_channel(
    RpcSession* session,
    const char* channel,
    uint32_t credits,
    uint64_t next_sequence,
    uint32_t* granted_credits,
    uint64_t* granted_sequence) {
    if(!session || !channel || !granted_credits || !granted_sequence) return false;
    size_t channel_index = 0u;
    if(poison_channel_open_at(
           &session->secure_channels, channel, credits, next_sequence, &channel_index) !=
       PoisonChannelResultOk) {
        return false;
    }
    const PoisonChannel* opened = poison_channel_get(&session->secure_channels, channel_index);
    if(!opened) return false;
    *granted_credits = opened->credits;
    *granted_sequence = opened->next_tx_sequence;
    return true;
}

bool rpc_session_add_secure_channel_credits(
    RpcSession* session,
    const char* channel,
    uint32_t credits) {
    if(!session || !channel || credits == 0u) return false;
    size_t channel_index = 0u;
    return poison_channel_find(&session->secure_channels, channel, &channel_index) &&
           poison_channel_add_credits(&session->secure_channels, channel_index, credits) ==
               PoisonChannelResultOk;
}

bool rpc_session_secure_channel_is_open(RpcSession* session, const char* channel) {
    if(!session || !channel) return false;
    size_t channel_index = 0u;
    return poison_channel_find(&session->secure_channels, channel, &channel_index);
}

bool rpc_session_register_cancellable(
    RpcSession* session,
    uint32_t command_id,
    RpcCancellationCallback callback,
    void* context) {
    if(!session || command_id == 0u || !callback || !context) return false;
    size_t available = RPC_CANCELLATION_SLOT_COUNT;
    for(size_t index = 0u; index < RPC_CANCELLATION_SLOT_COUNT; ++index) {
        RpcCancellationSlot* slot = &session->cancellations[index];
        if(slot->active && slot->command_id == command_id) return false;
        if(!slot->active && available == RPC_CANCELLATION_SLOT_COUNT) available = index;
    }
    if(available == RPC_CANCELLATION_SLOT_COUNT) return false;
    session->cancellations[available] = (RpcCancellationSlot){
        .active = true,
        .command_id = command_id,
        .callback = callback,
        .context = context,
    };
    return true;
}

void rpc_session_complete_cancellable(RpcSession* session, uint32_t command_id, void* context) {
    if(!session || command_id == 0u || !context) return;
    for(size_t index = 0u; index < RPC_CANCELLATION_SLOT_COUNT; ++index) {
        RpcCancellationSlot* slot = &session->cancellations[index];
        if(slot->active && slot->command_id == command_id && slot->context == context) {
            memset(slot, 0, sizeof(*slot));
            return;
        }
    }
}

static void rpc_cancel_request_process(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(context);
    RpcSession* session = context;
    bool accepted = false;
    uint64_t target_id = 0u;
    if(request->which_content == PB_Main_poison_cancel_request_tag && !request->has_next) {
        const PB_Poison_CancelRequest* cancel = &request->content.poison_cancel_request;
        target_id = cancel->command_id;
        if(target_id > 0u && target_id <= UINT32_MAX) {
            for(size_t index = 0u; index < RPC_CANCELLATION_SLOT_COUNT; ++index) {
                RpcCancellationSlot* slot = &session->cancellations[index];
                if(slot->active && slot->command_id == (uint32_t)target_id) {
                    accepted = slot->callback(slot->command_id, cancel->reason, slot->context);
                    if(accepted) memset(slot, 0, sizeof(*slot));
                    break;
                }
            }
        }
    }
    PB_Main response = PB_Main_init_zero;
    response.command_id = request->command_id;
    response.command_status = PB_CommandStatus_OK;
    response.which_content = PB_Main_poison_cancelled_tag;
    response.content.poison_cancelled.command_id = target_id;
    response.content.poison_cancelled.accepted = accepted;
    rpc_send_and_release(session, &response);
}

static void rpc_close_session_process(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(context);

    RpcSession* session = (RpcSession*)context;

    rpc_send_and_release_empty(session, request->command_id, PB_CommandStatus_OK);
    if(rpc_session_is_secure_dispatch_active(session))
        rpc_session_revoke_secure_transport(session);
    furi_mutex_acquire(session->callbacks_mutex, FuriWaitForever);
    if(session->closed_callback) {
        session->closed_callback(session->context);
    } else {
        FURI_LOG_W(TAG, "Session stop isn't processed by transport layer");
    }
    furi_mutex_release(session->callbacks_mutex);
}

void rpc_session_set_context(RpcSession* session, void* context) {
    furi_check(session);

    furi_mutex_acquire(session->callbacks_mutex, FuriWaitForever);
    session->context = context;
    furi_mutex_release(session->callbacks_mutex);
}

void rpc_session_set_close_callback(RpcSession* session, RpcSessionClosedCallback callback) {
    furi_check(session);

    furi_mutex_acquire(session->callbacks_mutex, FuriWaitForever);
    session->closed_callback = callback;
    furi_mutex_release(session->callbacks_mutex);
}

void rpc_session_set_send_bytes_callback(RpcSession* session, RpcSendBytesCallback callback) {
    furi_check(session);

    furi_mutex_acquire(session->callbacks_mutex, FuriWaitForever);
    session->send_bytes_callback = callback;
    furi_mutex_release(session->callbacks_mutex);
}

void rpc_session_set_buffer_is_empty_callback(
    RpcSession* session,
    RpcBufferIsEmptyCallback callback) {
    furi_check(session);

    furi_mutex_acquire(session->callbacks_mutex, FuriWaitForever);
    session->buffer_is_empty_callback = callback;
    furi_mutex_release(session->callbacks_mutex);
}

void rpc_session_set_terminated_callback(
    RpcSession* session,
    RpcSessionTerminatedCallback callback) {
    furi_check(session);

    furi_mutex_acquire(session->callbacks_mutex, FuriWaitForever);
    session->terminated_callback = callback;
    furi_mutex_release(session->callbacks_mutex);
}

void rpc_session_set_pairing_confirmation_callback(
    RpcSession* session,
    RpcPairingConfirmationCallback callback,
    void* context) {
    furi_check(session);
    furi_mutex_acquire(session->callbacks_mutex, FuriWaitForever);
    session->pairing_confirmation_callback = callback;
    session->pairing_confirmation_context = context;
    furi_mutex_release(session->callbacks_mutex);
}

bool rpc_session_request_pairing_confirmation(
    RpcSession* session,
    const char* confirmation_code,
    const char* fingerprint,
    const char* client_name,
    uint32_t requested_role,
    uint32_t requested_capabilities) {
    furi_check(session);
    furi_check(confirmation_code);
    furi_check(fingerprint);
    furi_check(client_name);
    furi_mutex_acquire(session->callbacks_mutex, FuriWaitForever);
    RpcPairingConfirmationCallback callback = session->pairing_confirmation_callback;
    void* callback_context = session->pairing_confirmation_context;
    furi_mutex_release(session->callbacks_mutex);
    if(callback) {
        return callback(
            callback_context,
            confirmation_code,
            fingerprint,
            client_name,
            requested_role,
            requested_capabilities);
    }

    FuriString* body = furi_string_alloc_printf(
        "%s\nCode %s\nID %s\nRole %lu Caps %08lX",
        client_name,
        confirmation_code,
        fingerprint,
        (unsigned long)requested_role,
        (unsigned long)requested_capabilities);
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "Pair secure client?", 64, 0, AlignCenter, AlignTop);
    dialog_message_set_text(message, furi_string_get_cstr(body), 64, 31, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, "Reject", NULL, "Approve");
    const bool approved = dialog_message_show(dialogs, message) == DialogMessageButtonRight;
    dialog_message_free(message);
    furi_record_close(RECORD_DIALOGS);
    furi_string_free(body);
    return approved;
}

void rpc_session_set_content_update_confirmation_callback(
    RpcSession* session,
    RpcContentUpdateConfirmationCallback callback,
    void* context) {
    furi_check(session);
    furi_mutex_acquire(session->callbacks_mutex, FuriWaitForever);
    session->content_update_confirmation_callback = callback;
    session->content_update_confirmation_context = context;
    furi_mutex_release(session->callbacks_mutex);
}

bool rpc_session_request_content_update_confirmation(
    RpcSession* session,
    const char* update_id,
    const char* candidate_digest,
    uint32_t content_type) {
    furi_check(session);
    furi_check(update_id);
    furi_check(candidate_digest);
    furi_mutex_acquire(session->callbacks_mutex, FuriWaitForever);
    RpcContentUpdateConfirmationCallback callback = session->content_update_confirmation_callback;
    void* callback_context = session->content_update_confirmation_context;
    furi_mutex_release(session->callbacks_mutex);
    if(callback) return callback(callback_context, update_id, candidate_digest, content_type);

    char digest_prefix[17u];
    memcpy(digest_prefix, candidate_digest, 16u);
    digest_prefix[16u] = '\0';
    FuriString* body = furi_string_alloc_printf(
        "%s\nDigest %s\nType %lu", update_id, digest_prefix, (unsigned long)content_type);
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "Activate update?", 64, 0, AlignCenter, AlignTop);
    dialog_message_set_text(message, furi_string_get_cstr(body), 64, 31, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, "Reject", NULL, "Activate");
    const bool approved = dialog_message_show(dialogs, message) == DialogMessageButtonRight;
    dialog_message_free(message);
    furi_record_close(RECORD_DIALOGS);
    furi_string_free(body);
    return approved;
}

bool rpc_session_request_profile_confirmation(
    RpcSession* session,
    const char* profile_id,
    const char* version,
    uint64_t capability_mask) {
    furi_check(session);
    furi_check(profile_id);
    furi_check(version);
    furi_mutex_acquire(session->callbacks_mutex, FuriWaitForever);
    RpcProfileConfirmationCallback callback = session->profile_confirmation_callback;
    void* callback_context = session->profile_confirmation_context;
    furi_mutex_release(session->callbacks_mutex);
    if(callback) return callback(callback_context, profile_id, version, capability_mask);
    FuriString* body = furi_string_alloc_printf(
        "%s\nVersion %s\nCaps %08lX", profile_id, version, (unsigned long)capability_mask);
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "Apply profile?", 64, 0, AlignCenter, AlignTop);
    dialog_message_set_text(message, furi_string_get_cstr(body), 64, 31, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, "Reject", NULL, "Apply");
    const bool approved = dialog_message_show(dialogs, message) == DialogMessageButtonRight;
    dialog_message_free(message);
    furi_record_close(RECORD_DIALOGS);
    furi_string_free(body);
    return approved;
}

void rpc_session_set_profile_confirmation_callback(
    RpcSession* session,
    RpcProfileConfirmationCallback callback,
    void* context) {
    furi_check(session);
    furi_mutex_acquire(session->callbacks_mutex, FuriWaitForever);
    session->profile_confirmation_callback = callback;
    session->profile_confirmation_context = context;
    furi_mutex_release(session->callbacks_mutex);
}

void rpc_session_activate_secure_transport(RpcSession* session, PoisonSession* poison_session) {
    furi_check(session);
    furi_check(poison_session);
    furi_check(poison_session->state == PoisonSessionActive);
    furi_check(furi_mutex_acquire(session->secure_mutex, FuriWaitForever) == FuriStatusOk);
    session->secure_transport_active = true;
    session->secure_session = poison_session;
    memcpy(session->secure_channel, "rpc", sizeof("rpc"));
    session->secure_acknowledgement = 0u;
    furi_check(furi_mutex_release(session->secure_mutex) == FuriStatusOk);
}

/* Doesn't forbid using rpc_feed_bytes() after session close - it's safe.
 * Because any bytes received in buffer will be flushed before next session.
 * If bytes get into stream buffer before it's get epmtified and this
 * command is gets processed - it's safe either. But case of it is quite
 * odd: client sends close request and sends command after.
 */
size_t rpc_session_feed(
    RpcSession* session,
    const uint8_t* encoded_bytes,
    size_t size,
    uint32_t timeout) {
    furi_check(session);
    furi_check(encoded_bytes);

    if(!size) return 0;

    size_t bytes_sent = furi_stream_buffer_send(session->stream, encoded_bytes, size, timeout);

    furi_thread_flags_set(furi_thread_get_id(session->thread), RpcEvtNewData);

    return bytes_sent;
}

size_t rpc_session_get_available_size(RpcSession* session) {
    furi_check(session);
    return furi_stream_buffer_spaces_available(session->stream);
}

bool rpc_pb_stream_read(pb_istream_t* istream, pb_byte_t* buf, size_t count) {
    furi_assert(istream);
    furi_assert(buf);
    RpcSession* session = istream->state;
    furi_assert(session);
    furi_assert(istream->bytes_left);

    if(session->terminate) {
        return false;
    }

    uint32_t flags = 0;
    size_t bytes_received = 0;

    while(1) {
        bytes_received += furi_stream_buffer_receive(
            session->stream, buf + bytes_received, count - bytes_received, 0);
        if(furi_stream_buffer_is_empty(session->stream)) {
            if(session->buffer_is_empty_callback) {
                session->buffer_is_empty_callback(session->context);
            }
        }
        if(session->decode_error) {
            /* never go out till RPC_EVENT_DISCONNECT come */
            bytes_received = 0;
        }
        if(count == bytes_received) {
            break;
        } else {
            flags = furi_thread_flags_wait(RPC_ALL_EVENTS, FuriFlagWaitAny, FuriWaitForever);
            if(flags & RpcEvtDisconnect) {
                if(furi_stream_buffer_is_empty(session->stream)) {
                    session->terminate = true;
                    istream->bytes_left = 0;
                    bytes_received = 0;
                    break;
                } else {
                    /* Save disconnect flag and continue reading buffer */
                    furi_thread_flags_set(furi_thread_get_id(session->thread), RpcEvtDisconnect);
                }
            } else if(flags & RpcEvtNewData) {
                // Just wake thread up
            }
        }
    }

#ifdef SRV_RPC_DEBUG
    rpc_debug_print_data("INPUT", buf, bytes_received);
#endif

    return count == bytes_received;
}

static bool rpc_pb_content_callback(pb_istream_t* stream, const pb_field_t* field, void** arg) {
    furi_assert(stream);
    furi_assert(arg);
    RpcSession* session = *arg;
    furi_assert(session);
    furi_assert(field);

    RpcHandler* handler = RpcHandlerDict_get(session->handlers, field->tag);

    if(handler && handler->decode_submessage) {
        void* handler_context = handler->context;
        return handler->decode_submessage(stream, field, &handler_context);
    }

    return true;
}

static PoisonCapability rpc_secure_required_capabilities(pb_size_t tag) {
    switch(tag) {
    case PB_Main_stop_session_tag:
    case PB_Main_system_ping_request_tag:
    case PB_Main_system_device_info_request_tag:
    case PB_Main_system_get_datetime_request_tag:
    case PB_Main_system_protobuf_version_request_tag:
    case PB_Main_system_power_info_request_tag:
    case PB_Main_property_get_request_tag:
    case PB_Main_desktop_is_locked_request_tag:
    case PB_Main_desktop_status_subscribe_request_tag:
    case PB_Main_desktop_status_unsubscribe_request_tag:
    case PB_Main_gpio_get_pin_mode_tag:
    case PB_Main_gpio_read_pin_tag:
    case PB_Main_gpio_get_otg_mode_tag:
    case PB_Main_poison_diagnostic_snapshot_request_tag:
    case PB_Main_poison_policy_request_tag:
    case PB_Main_poison_channel_open_tag:
    case PB_Main_poison_credit_update_tag:
    case PB_Main_poison_cancel_request_tag:
    case PB_Main_poison_resume_request_tag:
    case PB_Main_poison_workload_request_tag:
        return POISON_CAPABILITY_STATUS;
    case PB_Main_app_button_press_request_tag:
    case PB_Main_app_button_release_request_tag:
    case PB_Main_app_button_press_release_request_tag:
    case PB_Main_app_exit_request_tag:
    case PB_Main_gui_start_screen_stream_request_tag:
    case PB_Main_gui_stop_screen_stream_request_tag:
    case PB_Main_gui_send_input_event_request_tag:
    case PB_Main_gui_start_virtual_display_request_tag:
    case PB_Main_gui_stop_virtual_display_request_tag:
    case PB_Main_desktop_unlock_request_tag:
        return POISON_CAPABILITY_CONTROL;
    case PB_Main_app_start_request_tag:
    case PB_Main_app_load_file_request_tag:
    case PB_Main_app_get_error_request_tag:
    case PB_Main_app_data_exchange_request_tag:
        return POISON_CAPABILITY_LAUNCH;
    case PB_Main_storage_list_request_tag:
    case PB_Main_storage_read_request_tag:
    case PB_Main_storage_stat_request_tag:
    case PB_Main_storage_info_request_tag:
    case PB_Main_storage_md5sum_request_tag:
    case PB_Main_storage_timestamp_request_tag:
        return POISON_CAPABILITY_FILES;
    case PB_Main_storage_write_request_tag:
    case PB_Main_storage_delete_request_tag:
    case PB_Main_storage_mkdir_request_tag:
    case PB_Main_storage_rename_request_tag:
    case PB_Main_storage_backup_create_request_tag:
    case PB_Main_storage_backup_restore_request_tag:
    case PB_Main_storage_tar_extract_request_tag:
        return POISON_CAPABILITY_FILES | POISON_CAPABILITY_DESTRUCTIVE;
    case PB_Main_gpio_set_pin_mode_tag:
    case PB_Main_gpio_set_input_pull_tag:
    case PB_Main_gpio_write_pin_tag:
    case PB_Main_gpio_set_otg_mode_tag:
        return POISON_CAPABILITY_RADIO;
    case PB_Main_system_reboot_request_tag:
    case PB_Main_system_factory_reset_request_tag:
    case PB_Main_system_set_datetime_request_tag:
    case PB_Main_system_update_request_tag:
    case PB_Main_poison_content_update_request_tag:
    case PB_Main_poison_package_operation_request_tag:
        return POISON_CAPABILITY_DESTRUCTIVE;
    case PB_Main_poison_file_list_request_tag:
    case PB_Main_poison_file_transfer_begin_tag:
    case PB_Main_poison_file_transfer_chunk_tag:
    case PB_Main_poison_file_transfer_complete_tag:
    case PB_Main_poison_js_bundle_request_tag:
        return POISON_CAPABILITY_FILES;
    case PB_Main_poison_evidence_record_tag:
    case PB_Main_poison_case_tag:
    case PB_Main_poison_annotation_tag:
    case PB_Main_poison_export_manifest_tag:
        return POISON_CAPABILITY_EVIDENCE;
    case PB_Main_poison_tool_run_tag:
        return POISON_CAPABILITY_STATUS;
    case PB_Main_poison_app_command_tag:
    case PB_Main_poison_app_event_tag:
    case PB_Main_poison_profile_tag:
    case PB_Main_poison_profile_apply_tag:
        return POISON_CAPABILITY_CONTROL;
    default:
        return 0u;
    }
}

bool rpc_session_is_secure_dispatch_active(RpcSession* session) {
    if(!session) return false;
    furi_check(furi_mutex_acquire(session->secure_mutex, FuriWaitForever) == FuriStatusOk);
    const bool active = session->secure_dispatch_active;
    furi_check(furi_mutex_release(session->secure_mutex) == FuriStatusOk);
    return active;
}

bool rpc_session_get_secure_identity(RpcSession* session, uint64_t* session_id, PoisonRole* role) {
    if(!session || !session_id || !role) return false;
    furi_check(furi_mutex_acquire(session->secure_mutex, FuriWaitForever) == FuriStatusOk);
    const bool active = session->secure_dispatch_active && session->secure_session &&
                        session->secure_session->state == PoisonSessionActive;
    if(active) {
        *session_id = session->secure_session->session_id;
        *role = session->secure_session->role;
    }
    furi_check(furi_mutex_release(session->secure_mutex) == FuriStatusOk);
    return active;
}

bool rpc_session_get_secure_authorization(
    RpcSession* session,
    uint64_t* session_id,
    PoisonRole* role,
    PoisonCapability* capabilities) {
    if(!session || !session_id || !role || !capabilities) return false;
    furi_check(furi_mutex_acquire(session->secure_mutex, FuriWaitForever) == FuriStatusOk);
    const bool active = session->secure_dispatch_active && session->secure_session &&
                        session->secure_session->state == PoisonSessionActive;
    if(active) {
        *session_id = session->secure_session->session_id;
        *role = session->secure_session->role;
        *capabilities = session->secure_session->granted_capabilities;
    }
    furi_check(furi_mutex_release(session->secure_mutex) == FuriStatusOk);
    return active;
}

bool rpc_session_get_secure_actor_identity(RpcSession* session, uint8_t actor_identity_digest[32u]) {
    if(!session || !actor_identity_digest) return false;
    furi_check(furi_mutex_acquire(session->secure_mutex, FuriWaitForever) == FuriStatusOk);
    const bool active = session->secure_dispatch_active && session->secure_session &&
                        session->secure_session->state == PoisonSessionActive;
    if(active) {
        memcpy(actor_identity_digest, session->secure_session->client_identity_digest, 32u);
        uint8_t nonzero = 0u;
        for(size_t index = 0u; index < 32u; ++index)
            nonzero |= actor_identity_digest[index];
        if(nonzero == 0u) {
            memset(actor_identity_digest, 0, 32u);
            furi_check(furi_mutex_release(session->secure_mutex) == FuriStatusOk);
            return false;
        }
    }
    furi_check(furi_mutex_release(session->secure_mutex) == FuriStatusOk);
    if(!active) memset(actor_identity_digest, 0, 32u);
    return active;
}

bool rpc_session_get_audit_context(
    RpcSession* session,
    uint32_t command_id,
    uint8_t actor_digest[32u],
    uint8_t correlation_id[32u]) {
    if(!session || !actor_digest || !correlation_id) return false;
    uint64_t session_id = 0u;
    uint64_t acknowledgement = 0u;
    PoisonRole role = PoisonRoleObserver;
    PoisonCapability capabilities = 0u;
    furi_check(furi_mutex_acquire(session->secure_mutex, FuriWaitForever) == FuriStatusOk);
    const bool active = session->secure_dispatch_active && session->secure_session &&
                        session->secure_session->state == PoisonSessionActive;
    if(active) {
        session_id = session->secure_session->session_id;
        role = session->secure_session->role;
        capabilities = session->secure_session->granted_capabilities;
        acknowledgement = session->secure_acknowledgement;
    }
    furi_check(furi_mutex_release(session->secure_mutex) == FuriStatusOk);
    if(!active) return false;

    static const uint8_t actor_domain[] = "poison-rpc-actor-v1";
    static const uint8_t correlation_domain[] = "poison-rpc-correlation-v1";
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool ok = mbedtls_sha256_starts(&hash, 0) == 0 &&
              mbedtls_sha256_update(&hash, actor_domain, sizeof(actor_domain) - 1u) == 0 &&
              mbedtls_sha256_update(&hash, (const uint8_t*)&session_id, sizeof(session_id)) == 0 &&
              mbedtls_sha256_update(&hash, (const uint8_t*)&role, sizeof(role)) == 0 &&
              mbedtls_sha256_update(&hash, (const uint8_t*)&capabilities, sizeof(capabilities)) ==
                  0 &&
              mbedtls_sha256_finish(&hash, actor_digest) == 0;
    ok = ok && mbedtls_sha256_starts(&hash, 0) == 0 &&
         mbedtls_sha256_update(&hash, correlation_domain, sizeof(correlation_domain) - 1u) == 0 &&
         mbedtls_sha256_update(&hash, (const uint8_t*)&session_id, sizeof(session_id)) == 0 &&
         mbedtls_sha256_update(&hash, (const uint8_t*)&acknowledgement, sizeof(acknowledgement)) ==
             0 &&
         mbedtls_sha256_update(&hash, (const uint8_t*)&command_id, sizeof(command_id)) == 0 &&
         mbedtls_sha256_finish(&hash, correlation_id) == 0;
    mbedtls_sha256_free(&hash);
    if(!ok) {
        memset(actor_digest, 0, 32u);
        memset(correlation_id, 0, 32u);
    }
    return ok;
}

bool rpc_session_dispatch_secure_payload(
    RpcSession* session,
    PoisonSession* poison_session,
    const char* channel,
    uint64_t acknowledgement,
    const uint8_t* payload,
    size_t payload_length) {
    if(!session || !poison_session || !channel || strcmp(channel, "rpc") != 0 || !payload ||
       payload_length == 0u || payload_length > RPC_SECURE_REQUEST_PAYLOAD_MAX) {
        return false;
    }
    PB_Main inner = PB_Main_init_zero;
    inner.cb_content.funcs.decode = rpc_pb_content_callback;
    inner.cb_content.arg = session;
    pb_istream_t stream = pb_istream_from_buffer(payload, payload_length);
    if(!pb_decode(&stream, &PB_Main_msg, &inner) || stream.bytes_left != 0u ||
       inner.which_content == 0u || inner.which_content == PB_Main_poison_session_envelope_tag ||
       inner.which_content == PB_Main_poison_pairing_hello_tag ||
       inner.which_content == PB_Main_poison_pairing_challenge_tag ||
       inner.which_content == PB_Main_poison_pairing_confirm_tag) {
        pb_release(&PB_Main_msg, &inner);
        return false;
    }

    furi_check(furi_mutex_acquire(session->secure_mutex, FuriWaitForever) == FuriStatusOk);
    session->secure_transport_active = true;
    session->secure_dispatch_active = true;
    session->secure_session = poison_session;
    memcpy(session->secure_channel, channel, strlen(channel) + 1u);
    session->secure_acknowledgement = acknowledgement;
    furi_check(furi_mutex_release(session->secure_mutex) == FuriStatusOk);

    RpcHandler* handler = RpcHandlerDict_get(session->handlers, inner.which_content);
    const PoisonCapability required = rpc_secure_required_capabilities(inner.which_content);
    const bool channel_ready = inner.which_content == PB_Main_poison_channel_open_tag ||
                               rpc_session_secure_channel_is_open(session, channel);
    const bool authorized = channel_ready && required != 0u &&
                            (poison_session->granted_capabilities & required) == required;
    if(!authorized) {
        rpc_send_and_release_empty(
            session, inner.command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
    } else if(handler && handler->message_handler) {
        handler->message_handler(&inner, handler->context);
    } else {
        rpc_send_and_release_empty(
            session, inner.command_id, PB_CommandStatus_ERROR_NOT_IMPLEMENTED);
    }
    furi_check(furi_mutex_acquire(session->secure_mutex, FuriWaitForever) == FuriStatusOk);
    session->secure_dispatch_active = false;
    furi_check(furi_mutex_release(session->secure_mutex) == FuriStatusOk);
    pb_release(&PB_Main_msg, &inner);
    return true;
}

static bool rpc_session_plaintext_allowed(const RpcSession* session, pb_size_t content) {
    if(session->secure_transport_active) {
        return content == PB_Main_poison_session_envelope_tag;
    }

    if(session->owner != RpcOwnerUart) return true;

    return content == PB_Main_poison_pairing_hello_tag ||
           content == PB_Main_poison_pairing_confirm_tag ||
           content == PB_Main_poison_resume_request_tag ||
           content == PB_Main_poison_session_envelope_tag;
}

static int32_t rpc_session_worker(void* context) {
    furi_assert(context);
    RpcSession* session = (RpcSession*)context;
    Rpc* rpc = session->rpc;

    FURI_LOG_D(TAG, "Session started");

    while(1) {
        pb_istream_t istream = {
            .callback = rpc_pb_stream_read,
            .state = session,
            .errmsg = NULL,
            .bytes_left = SIZE_MAX,
        };

        bool message_decode_failed = false;

        if(pb_decode_ex(&istream, &PB_Main_msg, session->decoded_message, PB_DECODE_DELIMITED)) {
#ifdef SRV_RPC_DEBUG
            FURI_LOG_I(TAG, "INPUT:");
            rpc_debug_print_message(session->decoded_message);
#endif
            RpcHandler* handler =
                RpcHandlerDict_get(session->handlers, session->decoded_message->which_content);

            if(!rpc_session_plaintext_allowed(session, session->decoded_message->which_content)) {
                rpc_send_and_release_empty(
                    session,
                    session->decoded_message->command_id,
                    PB_CommandStatus_ERROR_INVALID_PARAMETERS);
            } else if(handler && handler->message_handler) {
                furi_check(furi_mutex_acquire(rpc->busy_mutex, FuriWaitForever) == FuriStatusOk);
                handler->message_handler(session->decoded_message, handler->context);
                furi_check(furi_mutex_release(rpc->busy_mutex) == FuriStatusOk);
            } else if(session->decoded_message->which_content == 0) {
                /* Receiving zeroes means message is 0-length, which
                 * is valid for proto3: all fields are filled with default values.
                 * 0 - is default value for which_content field.
                 * Mark it as decode error, because there is no content message
                 * in Main message with tag 0.
                 */
                message_decode_failed = true;
            } else if(!handler && !session->terminate) {
                FURI_LOG_E(
                    TAG,
                    "Message(%d) decoded, but not implemented",
                    session->decoded_message->which_content);
                rpc_send_and_release_empty(
                    session,
                    session->decoded_message->command_id,
                    PB_CommandStatus_ERROR_NOT_IMPLEMENTED);
            }
        } else {
            message_decode_failed = true;
        }

        if(message_decode_failed) {
            furi_stream_buffer_reset(session->stream);
            if(!session->terminate) {
                /* Protobuf can't determine start and end of message.
                 * Handle this by adding varint at beginning
                 * of a message (PB_ENCODE_DELIMITED). But decoding fail
                 * means we can't be sure next bytes are varint for next
                 * message, so the only way to close session.
                 * RPC itself can't make decision to close session. It has
                 * to notify:
                 * 1) down layer (transport)
                 * 2) other side (companion app)
                 * Who are responsible to handle RPC session lifecycle.
                 * Companion receives 2 messages: ERROR_DECODE and session_closed.
                 */
                FURI_LOG_E(TAG, "Decode failed, error: \'%.128s\'", PB_GET_ERROR(&istream));
                session->decode_error = true;
                rpc_send_and_release_empty(session, 0, PB_CommandStatus_ERROR_DECODE);
                furi_mutex_acquire(session->callbacks_mutex, FuriWaitForever);
                if(session->closed_callback) {
                    session->closed_callback(session->context);
                }
                furi_mutex_release(session->callbacks_mutex);

                if(session->owner == RpcOwnerBle) {
                    // Disconnect BLE session
                    FURI_LOG_E("RPC", "BLE session closed due to a decode error");
                    Bt* bt = furi_record_open(RECORD_BT);
                    bt_profile_restore_default(bt);
                    furi_record_close(RECORD_BT);
                    FURI_LOG_E("RPC", "Finished disconnecting the BLE session");
                }
            }
        }

        pb_release(&PB_Main_msg, session->decoded_message);

        if(session->terminate) {
            FURI_LOG_D(TAG, "Session terminated");
            break;
        }
    }

    return 0;
}

static void rpc_session_thread_pending_callback(void* context, uint32_t arg) {
    UNUSED(arg);
    RpcSession* session = (RpcSession*)context;

    for(size_t i = 0; i < COUNT_OF(rpc_systems); ++i) {
        if(rpc_systems[i].free) {
            (rpc_systems[i].free)(session->system_contexts[i]);
        }
    }
    free(session->system_contexts);
    free(session->decoded_message);
    RpcHandlerDict_clear(session->handlers);
    furi_stream_buffer_free(session->stream);

    furi_mutex_acquire(session->callbacks_mutex, FuriWaitForever);
    if(session->terminated_callback) {
        session->terminated_callback(session->context);
    }
    furi_mutex_release(session->callbacks_mutex);

    furi_mutex_free(session->callbacks_mutex);
    furi_mutex_free(session->secure_mutex);
    furi_thread_join(session->thread);
    furi_thread_free(session->thread);
    free(session);
}

static void
    rpc_session_thread_state_callback(FuriThread* thread, FuriThreadState state, void* context) {
    UNUSED(thread);
    if(state == FuriThreadStateStopped) {
        furi_timer_pending_callback(rpc_session_thread_pending_callback, context, 0);
    }
}

RpcSession* rpc_session_open(Rpc* rpc, RpcOwner owner) {
    furi_check(rpc);

    RpcSession* session = malloc(sizeof(RpcSession));
    session->callbacks_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    session->secure_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    session->stream = furi_stream_buffer_alloc(RPC_BUFFER_SIZE, 1);
    session->rpc = rpc;
    session->terminate = false;
    session->decode_error = false;
    session->owner = owner;
    session->pairing_confirmation_callback = NULL;
    session->pairing_confirmation_context = NULL;
    session->content_update_confirmation_callback = NULL;
    session->content_update_confirmation_context = NULL;
    session->profile_confirmation_callback = NULL;
    session->profile_confirmation_context = NULL;
    session->secure_dispatch_active = false;
    session->secure_transport_active = false;
    session->secure_session = NULL;
    session->secure_channel[0] = '\0';
    session->secure_acknowledgement = 0u;
    poison_channel_table_init(&session->secure_channels);
    RpcHandlerDict_init(session->handlers);

    session->decoded_message = malloc(sizeof(PB_Main));
    session->decoded_message->cb_content.funcs.decode = rpc_pb_content_callback;
    session->decoded_message->cb_content.arg = session;

    session->system_contexts = malloc(COUNT_OF(rpc_systems) * sizeof(void*));
    for(size_t i = 0; i < COUNT_OF(rpc_systems); ++i) {
        session->system_contexts[i] = rpc_systems[i].alloc(session);
    }

    RpcHandler rpc_handler = {
        .message_handler = rpc_close_session_process,
        .decode_submessage = NULL,
        .context = session,
    };
    rpc_add_handler(session, PB_Main_stop_session_tag, &rpc_handler);
    rpc_handler.message_handler = rpc_cancel_request_process;
    rpc_add_handler(session, PB_Main_poison_cancel_request_tag, &rpc_handler);

    session->thread = furi_thread_alloc_ex("RpcSessionWorker", 3072, rpc_session_worker, session);

    furi_thread_set_state_context(session->thread, session);
    furi_thread_set_state_callback(session->thread, rpc_session_thread_state_callback);

    furi_thread_start(session->thread);

    return session;
}

void rpc_session_close(RpcSession* session) {
    furi_check(session);
    furi_check(session->rpc);

    rpc_session_set_send_bytes_callback(session, NULL);
    rpc_session_set_close_callback(session, NULL);
    rpc_session_set_buffer_is_empty_callback(session, NULL);
    furi_thread_flags_set(furi_thread_get_id(session->thread), RpcEvtDisconnect);
}

static bool rpc_content_update_reconcile_boot(RpcPoisonContentUpdate* engine) {
    if(!engine || !engine->active || furi_hal_rtc_get_boot_mode() != FuriHalRtcBootModeNormal) {
        return false;
    }
    bool rollback_rearmed = false;
    if(engine->update.state == PoisonContentUpdateActivating) {
        RpcPoisonContentUpdate* previous = malloc(sizeof(*previous));
        if(!previous) return false;
        *previous = *engine;
        const bool healthy = poison_content_update_health_completed_at(
                                 &engine->update, POISON_CONTENT_UPDATE_HEALTH_COMPLETE_PATH) &&
                             rpc_poison_content_update_promote_last_known_good(
                                 engine, engine->activation_manifest_path);
        bool reconciled = false;
        if(healthy) {
            reconciled = poison_content_update_report_health(&engine->update, true);
            if(reconciled) {
                strcpy(engine->accepted_digest, engine->update.candidate_digest);
                engine->highest_release_sequence = engine->update.sequence;
            }
        } else if(
            engine->last_known_good_manifest_path[0] != '\0' &&
            update_operation_prepare(engine->last_known_good_manifest_path) ==
                UpdatePrepareResultOK) {
            reconciled = poison_content_update_report_health(&engine->update, false);
            rollback_rearmed = reconciled;
        } else {
            engine->update.state = PoisonContentUpdateQuarantined;
            reconciled = true;
        }
        reconciled = reconciled &&
                     rpc_poison_content_update_save(engine, POISON_CONTENT_UPDATE_STATE_PATH);
        if(reconciled) {
            uint8_t correlation_digest[32u];
            mbedtls_sha256_context hash;
            mbedtls_sha256_init(&hash);
            const bool correlated = mbedtls_sha256_starts(&hash, 0) == 0 &&
                                    mbedtls_sha256_update(
                                        &hash,
                                        (const uint8_t*)engine->update.candidate_digest,
                                        strlen(engine->update.candidate_digest)) == 0 &&
                                    mbedtls_sha256_finish(&hash, correlation_digest) == 0;
            mbedtls_sha256_free(&hash);
            if(correlated) {
                (void)poison_diagnostics_record(
                    poison_diagnostics_get(),
                    healthy ? PoisonDiagnosticUpdateHealth :
                              (rollback_rearmed ? PoisonDiagnosticUpdateRollback :
                                                  PoisonDiagnosticUpdateHealth),
                    healthy ?
                        "boot health accepted" :
                        (rollback_rearmed ? "boot rollback selected" : "boot health rejected"),
                    ((uint64_t)furi_get_tick() * 1000u) / furi_kernel_get_tick_frequency(),
                    correlation_digest);
            }
            memset(correlation_digest, 0, sizeof(correlation_digest));
            poison_content_update_health_clear_at(
                POISON_CONTENT_UPDATE_HEALTH_PENDING_PATH,
                POISON_CONTENT_UPDATE_HEALTH_COMPLETE_PATH);
        } else {
            if(rollback_rearmed) update_operation_disarm();
            rollback_rearmed = false;
            *engine = *previous;
        }
        memset(previous, 0, sizeof(*previous));
        free(previous);
    } else if(
        engine->update.state == PoisonContentUpdateHealthy ||
        engine->update.state == PoisonContentUpdateRolledBack ||
        engine->update.state == PoisonContentUpdateQuarantined) {
        poison_content_update_health_clear_at(
            POISON_CONTENT_UPDATE_HEALTH_PENDING_PATH, POISON_CONTENT_UPDATE_HEALTH_COMPLETE_PATH);
    }
    return rollback_rearmed;
}

static void rpc_content_update_reset(void* context, uint32_t arg) {
    UNUSED(context);
    UNUSED(arg);
    furi_delay_ms(100u);
    furi_hal_power_reset();
}

void rpc_on_system_start(void) {
    Rpc* rpc = malloc(sizeof(Rpc));

    poison_pairing_registry_on_system_start();

    rpc->busy_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    rpc->resume_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    for(size_t index = 0u; index < RPC_RESUME_SLOT_COUNT; ++index)
        poison_session_resume_slot_init(&rpc->resume_slots[index]);
    rpc_poison_content_update_init(&rpc->content_update);
    const bool state_loaded =
        rpc_poison_content_update_load(&rpc->content_update, POISON_CONTENT_UPDATE_STATE_PATH);
    if(rpc->content_update.last_known_good_manifest_path[0] == '\0' &&
       rpc_poison_content_update_set_last_known_good(
           &rpc->content_update, POISON_CONTENT_UPDATE_BOOTSTRAP_LKG_PATH)) {
        (void)rpc_poison_content_update_save(
            &rpc->content_update, POISON_CONTENT_UPDATE_STATE_PATH);
    }
    const bool rollback_rearmed = state_loaded &&
                                  rpc_content_update_reconcile_boot(&rpc->content_update);

    CliRegistry* registry = furi_record_open(RECORD_CLI);
    cli_registry_add_command(
        registry,
        "start_rpc_session",
        CliCommandFlagParallelSafe,
        rpc_cli_command_start_session,
        rpc);
    furi_record_close(RECORD_CLI);

    furi_record_create(RECORD_RPC, rpc);
    if(rollback_rearmed) furi_timer_pending_callback(rpc_content_update_reset, NULL, 0u);
}

void rpc_add_handler(RpcSession* session, pb_size_t message_tag, RpcHandler* handler) {
    furi_assert(RpcHandlerDict_get(session->handlers, message_tag) == NULL);

    RpcHandlerDict_set_at(session->handlers, message_tag, *handler);
}

static void rpc_send_plain(RpcSession* session, PB_Main* message) {
    furi_assert(session);
    furi_assert(message);

    pb_ostream_t ostream = PB_OSTREAM_SIZING;

#ifdef SRV_RPC_DEBUG
    FURI_LOG_I(TAG, "OUTPUT:");
    rpc_debug_print_message(message);
#endif

    bool result = pb_encode_ex(&ostream, &PB_Main_msg, message, PB_ENCODE_DELIMITED);
    furi_check(result && ostream.bytes_written);

    uint8_t* buffer = malloc(ostream.bytes_written);
    ostream = pb_ostream_from_buffer(buffer, ostream.bytes_written);

    pb_encode_ex(&ostream, &PB_Main_msg, message, PB_ENCODE_DELIMITED);

#ifdef SRV_RPC_DEBUG
    rpc_debug_print_data("OUTPUT", buffer, ostream.bytes_written);
#endif

    furi_mutex_acquire(session->callbacks_mutex, FuriWaitForever);
    if(session->send_bytes_callback) {
        session->send_bytes_callback(session->context, buffer, ostream.bytes_written);
    }
    furi_mutex_release(session->callbacks_mutex);

    free(buffer);
}

static void rpc_send_secure(RpcSession* session, PB_Main* message) {
    const size_t payload_capacity = sizeof(((PB_Poison_SessionEnvelope*)0)->payload.bytes);
    uint8_t* plaintext = malloc(payload_capacity);
    uint8_t* ciphertext = malloc(payload_capacity);
    furi_check(plaintext);
    furi_check(ciphertext);
    pb_ostream_t inner_stream = pb_ostream_from_buffer(plaintext, payload_capacity);
    PB_Main fallback = PB_Main_init_zero;
    PB_Main* encoded_message = message;
    if(!pb_encode(&inner_stream, &PB_Main_msg, encoded_message)) {
        fallback.command_id = message->command_id;
        fallback.command_status = PB_CommandStatus_ERROR_INVALID_PARAMETERS;
        fallback.which_content = PB_Main_empty_tag;
        inner_stream = pb_ostream_from_buffer(plaintext, payload_capacity);
        furi_check(pb_encode(&inner_stream, &PB_Main_msg, &fallback));
    }

    PB_Main outer = PB_Main_init_zero;
    outer.command_id = message->command_id;
    outer.command_status = PB_CommandStatus_OK;
    outer.which_content = PB_Main_poison_session_envelope_tag;
    PB_Poison_SessionEnvelope* envelope = &outer.content.poison_session_envelope;
    PoisonSession* poison_session = session->secure_session;
    furi_check(poison_session);
    envelope->protocol_version = poison_session->protocol_version;
    envelope->session_id = poison_session->session_id;
    envelope->acknowledgement = session->secure_acknowledgement;
    memcpy(envelope->channel, session->secure_channel, strlen(session->secure_channel) + 1u);
    envelope->payload.size = inner_stream.bytes_written;
    envelope->authentication_tag.size = POISON_SESSION_AUTH_TAG_BYTES;
    furi_check(
        poison_session_encrypt_tx(
            poison_session,
            envelope->acknowledgement,
            envelope->channel,
            plaintext,
            inner_stream.bytes_written,
            &envelope->sequence,
            ciphertext,
            envelope->authentication_tag.bytes) == PoisonSessionResultOk);
    memcpy(envelope->payload.bytes, ciphertext, inner_stream.bytes_written);
    memset(plaintext, 0, inner_stream.bytes_written);
    memset(ciphertext, 0, inner_stream.bytes_written);
    rpc_send_plain(session, &outer);
    free(ciphertext);
    free(plaintext);
}

void rpc_send(RpcSession* session, PB_Main* message) {
    furi_assert(session);
    furi_assert(message);
    furi_check(furi_mutex_acquire(session->secure_mutex, FuriWaitForever) == FuriStatusOk);
    const bool secure = session->secure_transport_active && session->secure_session &&
                        session->secure_session->state == PoisonSessionActive;
    if(secure) {
        rpc_send_secure(session, message);
    } else {
        rpc_send_plain(session, message);
    }
    furi_check(furi_mutex_release(session->secure_mutex) == FuriStatusOk);
}

void rpc_send_and_release(RpcSession* session, PB_Main* message) {
    rpc_send(session, message);
    pb_release(&PB_Main_msg, message);
}

void rpc_send_and_release_empty(RpcSession* session, uint32_t command_id, PB_CommandStatus status) {
    furi_assert(session);

    PB_Main message = {
        .command_id = command_id,
        .command_status = status,
        .has_next = false,
        .which_content = PB_Main_empty_tag,
    };

    rpc_send_and_release(session, &message);
    pb_release(&PB_Main_msg, &message);
}
