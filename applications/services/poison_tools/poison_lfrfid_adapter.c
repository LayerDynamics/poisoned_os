#include "poison_lfrfid_adapter.h"

#include <furi.h>
#include <lfrfid/lfrfid_worker.h>
#include <lfrfid/protocols/lfrfid_protocols.h>
#include <string.h>
#include <toolbox/protocols/protocol_dict.h>

#define POISON_LFRFID_DETECTED_FLAG (1u << 0)

struct PoisonLfRfidSession {
    ProtocolDict* dictionary;
    LFRFIDWorker* worker;
    FuriEventFlag* events;
    ProtocolId protocol;
    bool started;
};

static void
    poison_lfrfid_callback(LFRFIDWorkerReadResult result, ProtocolId protocol, void* context) {
    PoisonLfRfidSession* session = context;
    if(session && result == LFRFIDWorkerReadDone) {
        session->protocol = protocol;
        furi_event_flag_set(session->events, POISON_LFRFID_DETECTED_FLAG);
    }
}

PoisonLfRfidSession* poison_lfrfid_session_alloc(void) {
    PoisonLfRfidSession* session = malloc(sizeof(*session));
    if(!session) return NULL;
    memset(session, 0, sizeof(*session));
    session->protocol = PROTOCOL_NO;
    session->dictionary = protocol_dict_alloc(lfrfid_protocols, LFRFIDProtocolMax);
    session->events = furi_event_flag_alloc();
    if(!session->dictionary || !session->events) {
        poison_lfrfid_session_free(session);
        return NULL;
    }
    session->worker = lfrfid_worker_alloc(session->dictionary);
    if(!session->worker) {
        poison_lfrfid_session_free(session);
        return NULL;
    }
    return session;
}

bool poison_lfrfid_session_start(PoisonLfRfidSession* session) {
    if(!session || session->started) return false;
    session->protocol = PROTOCOL_NO;
    furi_event_flag_clear(session->events, POISON_LFRFID_DETECTED_FLAG);
    lfrfid_worker_start_thread(session->worker);
    lfrfid_worker_read_start(
        session->worker, LFRFIDWorkerReadTypeAuto, poison_lfrfid_callback, session);
    session->started = true;
    return true;
}

bool poison_lfrfid_session_wait(
    PoisonLfRfidSession* session,
    uint32_t timeout_ms,
    PoisonLfRfidDetection* detection) {
    if(!session || !session->started || !detection || timeout_ms == 0u || timeout_ms > 60000u)
        return false;
    const uint32_t flags = furi_event_flag_wait(
        session->events, POISON_LFRFID_DETECTED_FLAG, FuriFlagWaitAny, timeout_ms);
    if((flags & POISON_LFRFID_DETECTED_FLAG) == 0u || session->protocol == PROTOCOL_NO)
        return false;
    const size_t size = protocol_dict_get_data_size(session->dictionary, session->protocol);
    const char* name = protocol_dict_get_name(session->dictionary, session->protocol);
    if(!name || size == 0u || size > sizeof(detection->data) || strlen(name) > 32u) return false;
    memset(detection, 0, sizeof(*detection));
    strcpy(detection->protocol, name);
    detection->data_size = size;
    protocol_dict_get_data(session->dictionary, session->protocol, detection->data, size);
    return true;
}

void poison_lfrfid_session_stop(PoisonLfRfidSession* session) {
    if(!session || !session->started) return;
    lfrfid_worker_stop(session->worker);
    lfrfid_worker_stop_thread(session->worker);
    session->started = false;
}

void poison_lfrfid_session_free(PoisonLfRfidSession* session) {
    if(!session) return;
    poison_lfrfid_session_stop(session);
    if(session->worker) lfrfid_worker_free(session->worker);
    if(session->dictionary) protocol_dict_free(session->dictionary);
    if(session->events) furi_event_flag_free(session->events);
    free(session);
}
