#include "rpc_i.h"

#include "../poison_audit/poison_audit_i.h"

#include <string.h>

typedef struct {
    RpcSession* session;
} RpcPoisonAudit;

static void rpc_poison_audit_snapshot_process(const PB_Main* request, void* context) {
    RpcPoisonAudit* audit = context;
    uint64_t session_id = 0u;
    PoisonRole role = PoisonRoleObserver;
    const PB_Poison_AuditSnapshotRequest* input = &request->content.poison_audit_snapshot_request;
    if(request->which_content != PB_Main_poison_audit_snapshot_request_tag || request->has_next ||
       !rpc_session_is_secure_dispatch_active(audit->session) ||
       !rpc_session_get_secure_identity(audit->session, &session_id, &role) || session_id == 0u ||
       role > PoisonRoleObserver || input->max_events == 0u ||
       input->max_events > POISON_AUDIT_RING_SIZE) {
        rpc_send_and_release_empty(
            audit->session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }

    PoisonAuditEvent* events = malloc(sizeof(PoisonAuditEvent) * input->max_events);
    furi_check(events);
    bool truncated = false;
    uint64_t next_event_id = 0u;
    uint8_t last_digest[POISON_AUDIT_DIGEST_BYTES];
    const size_t event_count = poison_audit_snapshot(
        poison_audit_get(),
        input->after_event_id,
        events,
        input->max_events,
        &truncated,
        &next_event_id,
        last_digest);
    PB_Main* response = rpc_message_alloc();

    for(size_t index = 0u; index < event_count; ++index) {
        *response = (PB_Main)PB_Main_init_zero;
        response->command_id = request->command_id;
        response->command_status = PB_CommandStatus_OK;
        response->has_next = true;
        response->which_content = PB_Main_poison_audit_event_tag;
        PB_Poison_AuditEvent* output = &response->content.poison_audit_event;
        const PoisonAuditEvent* event = &events[index];
        output->event_id = event->event_id;
        output->prior_digest.size = POISON_AUDIT_DIGEST_BYTES;
        memcpy(output->prior_digest.bytes, event->prior_digest, POISON_AUDIT_DIGEST_BYTES);
        output->actor_digest.size = POISON_AUDIT_DIGEST_BYTES;
        memcpy(output->actor_digest.bytes, event->actor_digest, POISON_AUDIT_DIGEST_BYTES);
        strcpy(output->action, event->action);
        strcpy(output->resource, event->resource);
        output->decision = (PB_Poison_AuditDecision)event->decision;
        output->timestamp_ms = event->timestamp_ms;
        output->correlation_id.size = POISON_AUDIT_DIGEST_BYTES;
        memcpy(output->correlation_id.bytes, event->correlation_id, POISON_AUDIT_DIGEST_BYTES);
        strcpy(output->safe_metadata, event->safe_metadata);
        output->digest.size = POISON_AUDIT_DIGEST_BYTES;
        memcpy(output->digest.bytes, event->digest, POISON_AUDIT_DIGEST_BYTES);
        rpc_send(audit->session, response);
    }
    memset(events, 0, sizeof(PoisonAuditEvent) * input->max_events);
    free(events);

    *response = (PB_Main)PB_Main_init_zero;
    response->command_id = request->command_id;
    response->command_status = PB_CommandStatus_OK;
    response->which_content = PB_Main_poison_audit_snapshot_end_tag;
    response->content.poison_audit_snapshot_end.next_event_id = next_event_id;
    response->content.poison_audit_snapshot_end.last_digest.size = POISON_AUDIT_DIGEST_BYTES;
    memcpy(
        response->content.poison_audit_snapshot_end.last_digest.bytes,
        last_digest,
        POISON_AUDIT_DIGEST_BYTES);
    response->content.poison_audit_snapshot_end.truncated = truncated;
    rpc_send(audit->session, response);
    free(response);
}

void* rpc_system_poison_audit_alloc(RpcSession* session) {
    RpcPoisonAudit* audit = malloc(sizeof(*audit));
    furi_check(audit);
    audit->session = session;
    RpcHandler handler = {
        .message_handler = rpc_poison_audit_snapshot_process,
        .decode_submessage = NULL,
        .context = audit,
    };
    rpc_add_handler(session, PB_Main_poison_audit_snapshot_request_tag, &handler);
    return audit;
}

void rpc_system_poison_audit_free(void* context) {
    if(!context) return;
    RpcPoisonAudit* audit = context;
    memset(audit, 0, sizeof(*audit));
    free(audit);
}
