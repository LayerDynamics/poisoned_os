#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "rpc_poison_app.h"
#include "rpc_i.h"
#include "../poison_app/poison_app_i.h"

typedef struct {
    RpcSession* session;
    bool subscribed;
    bool receiving_command;
    uint32_t expected_chunks;
    uint32_t next_chunk;
    size_t payload_size;
    char app_id[65];
    char run_id[65];
    char command_id[65];
    char payload_json[4097];
} RpcPoisonApp;

#define RPC_POISON_APP_MAX_CHUNKS  11u
#define RPC_POISON_APP_ID_CAPACITY (sizeof(((PB_Poison_AppCommand*)0)->app_id) / sizeof(char))

/* RPC integration point: malformed commands are rejected before dispatch.
 * The transport-specific caller supplies the already-authenticated session
 * authorization decision; this function never grants capabilities itself. */
bool rpc_poison_app_command_is_bounded(
    const char* app_id,
    const char* command_id,
    const char* payload_json) {
    return app_id && command_id && payload_json && app_id[0] != '\0' && command_id[0] != '\0' &&
           strnlen(app_id, RPC_POISON_APP_ID_CAPACITY) < RPC_POISON_APP_ID_CAPACITY &&
           strnlen(command_id, RPC_POISON_APP_ID_CAPACITY) < RPC_POISON_APP_ID_CAPACITY &&
           strnlen(payload_json, 4097u) < 4097u;
}

bool rpc_poison_app_accept_event_authenticated(
    PoisonSession* session,
    uint32_t protocol_version,
    uint64_t sequence,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* payload,
    size_t payload_length,
    const uint8_t authentication_tag[POISON_SESSION_AUTH_TAG_BYTES],
    PoisonAppRun* run,
    const PoisonAppEvent* event) {
    if(!channel || strcmp(channel, "app") != 0) return false;
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
    return poison_app_accept_event(run, event);
}

static void rpc_poison_app_event_base(
    PB_Main* response,
    const PoisonAppEvent* event,
    uint32_t chunk_index,
    uint32_t chunk_count) {
    *response = (PB_Main)PB_Main_init_zero;
    response->command_id = 0u;
    response->command_status = PB_CommandStatus_OK;
    response->which_content = PB_Main_poison_app_event_tag;
    PB_Poison_AppEvent* output = &response->content.poison_app_event;
    strcpy(output->app_id, event->app_id);
    strcpy(output->run_id, event->run_id);
    strcpy(output->event_id, event->event_id);
    output->sequence = event->sequence;
    output->protocol_version = POISON_APP_PROTOCOL_VERSION;
    output->chunk_index = chunk_index;
    output->chunk_count = chunk_count;
}

static void rpc_poison_app_copy_chunk(
    PB_Poison_AppEvent_message_chunk_t* output,
    const char* source,
    size_t offset) {
    const size_t remaining = strlen(source) - offset;
    output->size = remaining < sizeof(output->bytes) ? remaining : sizeof(output->bytes);
    memcpy(output->bytes, source + offset, output->size);
}

static uint32_t rpc_poison_app_chunk_count(size_t length) {
    const size_t capacity = sizeof(((PB_Poison_AppEvent_message_chunk_t*)0)->bytes);
    return (uint32_t)((length + capacity - 1u) / capacity);
}

static void rpc_poison_app_send_text_events(
    RpcPoisonApp* rpc_app,
    const PoisonAppEvent* event,
    const char* first,
    const char* second) {
    const size_t capacity = sizeof(((PB_Poison_AppEvent_message_chunk_t*)0)->bytes);
    const uint32_t first_count = rpc_poison_app_chunk_count(strlen(first));
    const uint32_t second_count = second ? rpc_poison_app_chunk_count(strlen(second)) : 0u;
    const uint32_t chunk_count = first_count + second_count;
    for(uint32_t index = 0u; index < chunk_count; index++) {
        PB_Main* response = malloc(sizeof(PB_Main));
        furi_check(response);
        rpc_poison_app_event_base(response, event, index, chunk_count);
        PB_Poison_AppEvent* output = &response->content.poison_app_event;
        if(event->kind == PoisonAppEventLog) {
            output->which_kind = PB_Poison_AppEvent_log_tag;
            strcpy(output->kind.log.level, event->level);
            rpc_poison_app_copy_chunk(&output->message_chunk, first, index * capacity);
        } else if(event->kind == PoisonAppEventResult) {
            output->which_kind = PB_Poison_AppEvent_result_tag;
            output->kind.result.success = event->success;
            rpc_poison_app_copy_chunk(&output->message_chunk, first, index * capacity);
        } else if(event->kind == PoisonAppEventForm) {
            output->which_kind = PB_Poison_AppEvent_form_tag;
            rpc_poison_app_copy_chunk(
                (PB_Poison_AppEvent_message_chunk_t*)&output->schema_json_chunk,
                first,
                index * capacity);
        } else {
            output->which_kind = PB_Poison_AppEvent_table_tag;
            if(index < first_count) {
                rpc_poison_app_copy_chunk(
                    (PB_Poison_AppEvent_message_chunk_t*)&output->schema_json_chunk,
                    first,
                    index * capacity);
            } else {
                rpc_poison_app_copy_chunk(
                    (PB_Poison_AppEvent_message_chunk_t*)&output->rows_json_chunk,
                    second,
                    (index - first_count) * capacity);
            }
        }
        rpc_send_and_release(rpc_app->session, response);
        free(response);
    }
}

static void rpc_poison_app_send_event(const PoisonAppEvent* event, void* context) {
    RpcPoisonApp* rpc_app = context;
    if(event->kind == PoisonAppEventLog || event->kind == PoisonAppEventResult ||
       event->kind == PoisonAppEventForm || event->kind == PoisonAppEventTable) {
        const char* first = event->kind == PoisonAppEventForm  ? event->schema_json :
                            event->kind == PoisonAppEventTable ? event->schema_json :
                                                                 event->message;
        rpc_poison_app_send_text_events(
            rpc_app, event, first, event->kind == PoisonAppEventTable ? event->rows_json : NULL);
        return;
    }
    PB_Main* response = malloc(sizeof(PB_Main));
    furi_check(response);
    rpc_poison_app_event_base(response, event, 0u, 1u);
    PB_Poison_AppEvent* output = &response->content.poison_app_event;
    switch(event->kind) {
    case PoisonAppEventProgress:
        output->which_kind = PB_Poison_AppEvent_progress_tag;
        output->kind.progress.percent = event->percent;
        strcpy(output->kind.progress.label, event->label);
        break;
    case PoisonAppEventArtifact:
        output->which_kind = PB_Poison_AppEvent_artifact_tag;
        strcpy(output->kind.artifact.name, event->artifact_name);
        strcpy(output->kind.artifact.path, event->artifact_path);
        strcpy(output->kind.artifact.sha256, event->artifact_sha256);
        output->kind.artifact.size = event->artifact_size;
        break;
    default:
        free(response);
        return;
    }
    rpc_send_and_release(rpc_app->session, response);
    free(response);
}

static void rpc_poison_app_reset_command(RpcPoisonApp* rpc_app) {
    rpc_app->receiving_command = false;
    rpc_app->expected_chunks = 0u;
    rpc_app->next_chunk = 0u;
    rpc_app->payload_size = 0u;
    memset(rpc_app->payload_json, 0, sizeof(rpc_app->payload_json));
}

static bool rpc_poison_app_collect_command(
    RpcPoisonApp* rpc_app,
    const PB_Poison_AppCommand* input,
    bool* complete) {
    *complete = false;
    if(input->chunk_count == 0u) {
        if(input->payload_chunk.size != 0u || rpc_app->receiving_command) return false;
        *complete = true;
        return true;
    }
    if(input->chunk_count > RPC_POISON_APP_MAX_CHUNKS ||
       input->chunk_index >= input->chunk_count || input->payload_json[0] != '\0') {
        rpc_poison_app_reset_command(rpc_app);
        return false;
    }
    if(input->chunk_index == 0u) {
        rpc_poison_app_reset_command(rpc_app);
        rpc_app->receiving_command = true;
        rpc_app->expected_chunks = input->chunk_count;
        strcpy(rpc_app->app_id, input->app_id);
        strcpy(rpc_app->run_id, input->run_id);
        strcpy(rpc_app->command_id, input->command_id);
    }
    if(!rpc_app->receiving_command || input->chunk_count != rpc_app->expected_chunks ||
       input->chunk_index != rpc_app->next_chunk || strcmp(input->app_id, rpc_app->app_id) != 0 ||
       strcmp(input->run_id, rpc_app->run_id) != 0 ||
       strcmp(input->command_id, rpc_app->command_id) != 0 || input->payload_chunk.size == 0u ||
       rpc_app->payload_size + input->payload_chunk.size > 4096u ||
       memchr(input->payload_chunk.bytes, '\0', input->payload_chunk.size)) {
        rpc_poison_app_reset_command(rpc_app);
        return false;
    }
    memcpy(
        rpc_app->payload_json + rpc_app->payload_size,
        input->payload_chunk.bytes,
        input->payload_chunk.size);
    rpc_app->payload_size += input->payload_chunk.size;
    rpc_app->next_chunk++;
    if(rpc_app->next_chunk == rpc_app->expected_chunks) {
        rpc_app->payload_json[rpc_app->payload_size] = '\0';
        rpc_app->receiving_command = false;
        *complete = true;
    }
    return true;
}

static void rpc_poison_app_command_process(const PB_Main* request, void* context) {
    RpcPoisonApp* rpc_app = context;
    if(request->which_content != PB_Main_poison_app_command_tag || request->has_next ||
       !rpc_app->subscribed || !rpc_session_is_secure_dispatch_active(rpc_app->session)) {
        rpc_send_and_release_empty(
            rpc_app->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    const PB_Poison_AppCommand* input = &request->content.poison_app_command;
    bool complete = false;
    if(!rpc_poison_app_collect_command(rpc_app, input, &complete)) {
        rpc_send_and_release_empty(
            rpc_app->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    if(!complete) {
        rpc_send_and_release_empty(rpc_app->session, request->command_id, PB_CommandStatus_OK);
        return;
    }
    PoisonAppCommand command = {
        .protocol_version = input->protocol_version,
        .app_id = input->app_id,
        .run_id = input->run_id,
        .command_id = input->command_id,
        .payload_json = input->chunk_count == 0u ? input->payload_json : rpc_app->payload_json,
        .cancel = input->cancel,
    };
    const bool accepted =
        rpc_poison_app_command_is_bounded(input->app_id, input->command_id, input->payload_json) &&
        poison_app_dispatch_command(&command);
    rpc_send_and_release_empty(
        rpc_app->session,
        request->command_id,
        accepted ? PB_CommandStatus_OK : PB_CommandStatus_ERROR_INVALID_PARAMETERS);
    rpc_poison_app_reset_command(rpc_app);
}

static void rpc_poison_app_event_process(const PB_Main* request, void* context) {
    RpcPoisonApp* rpc_app = context;
    rpc_send_and_release_empty(
        rpc_app->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
}

void* rpc_system_poison_app_alloc(RpcSession* session) {
    RpcPoisonApp* rpc_app = malloc(sizeof(*rpc_app));
    rpc_app->session = session;
    rpc_app->subscribed = poison_app_event_subscribe(rpc_poison_app_send_event, rpc_app);
    RpcHandler handler = {
        .message_handler = rpc_poison_app_command_process,
        .decode_submessage = NULL,
        .context = rpc_app,
    };
    rpc_add_handler(session, PB_Main_poison_app_command_tag, &handler);
    handler.message_handler = rpc_poison_app_event_process;
    rpc_add_handler(session, PB_Main_poison_app_event_tag, &handler);
    return rpc_app;
}

void rpc_system_poison_app_free(void* context) {
    if(!context) return;
    RpcPoisonApp* rpc_app = context;
    if(rpc_app->subscribed) poison_app_event_unsubscribe(rpc_app);
    memset(rpc_app, 0, sizeof(*rpc_app));
    free(rpc_app);
}
