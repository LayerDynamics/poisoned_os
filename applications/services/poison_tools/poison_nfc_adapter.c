#include "poison_nfc_adapter.h"

#include <furi.h>
#include <nfc/nfc.h>
#include <nfc/nfc_scanner.h>
#include <string.h>

#define POISON_NFC_DETECTED_FLAG (1u << 0)

struct PoisonNfcSession {
    Nfc* nfc;
    NfcScanner* scanner;
    FuriEventFlag* events;
    PoisonNfcDetection detection;
    bool started;
};

static void poison_nfc_scanner_callback(NfcScannerEvent event, void* context) {
    PoisonNfcSession* session = context;
    if(!session || event.type != NfcScannerEventTypeDetected || event.data.protocol_num == 0u ||
       !event.data.protocols) {
        return;
    }
    session->detection.protocol_count =
        event.data.protocol_num < NfcProtocolNum ? event.data.protocol_num : NfcProtocolNum;
    memcpy(
        session->detection.protocols,
        event.data.protocols,
        session->detection.protocol_count * sizeof(NfcProtocol));
    furi_event_flag_set(session->events, POISON_NFC_DETECTED_FLAG);
}

PoisonNfcSession* poison_nfc_session_alloc(void) {
    PoisonNfcSession* session = malloc(sizeof(*session));
    if(!session) return NULL;
    memset(session, 0, sizeof(*session));
    session->nfc = nfc_alloc();
    session->events = furi_event_flag_alloc();
    if(!session->nfc || !session->events) {
        poison_nfc_session_free(session);
        return NULL;
    }
    session->scanner = nfc_scanner_alloc(session->nfc);
    if(!session->scanner) {
        poison_nfc_session_free(session);
        return NULL;
    }
    return session;
}

bool poison_nfc_session_start(PoisonNfcSession* session) {
    if(!session || session->started) return false;
    memset(&session->detection, 0, sizeof(session->detection));
    furi_event_flag_clear(session->events, POISON_NFC_DETECTED_FLAG);
    nfc_scanner_start(session->scanner, poison_nfc_scanner_callback, session);
    session->started = true;
    return true;
}

bool poison_nfc_session_wait(
    PoisonNfcSession* session,
    uint32_t timeout_ms,
    PoisonNfcDetection* detection) {
    if(!session || !session->started || !detection || timeout_ms == 0u || timeout_ms > 60000u)
        return false;
    const uint32_t flags = furi_event_flag_wait(
        session->events, POISON_NFC_DETECTED_FLAG, FuriFlagWaitAny, timeout_ms);
    if((flags & POISON_NFC_DETECTED_FLAG) == 0u) return false;
    *detection = session->detection;
    return detection->protocol_count != 0u;
}

void poison_nfc_session_stop(PoisonNfcSession* session) {
    if(!session || !session->started) return;
    nfc_scanner_stop(session->scanner);
    session->started = false;
}

void poison_nfc_session_free(PoisonNfcSession* session) {
    if(!session) return;
    poison_nfc_session_stop(session);
    if(session->scanner) nfc_scanner_free(session->scanner);
    if(session->nfc) nfc_free(session->nfc);
    if(session->events) furi_event_flag_free(session->events);
    free(session);
}

const char* poison_nfc_protocol_name(NfcProtocol protocol) {
    static const char* const names[NfcProtocolNum] = {
        [NfcProtocolIso14443_3a] = "iso14443-3a",
        [NfcProtocolIso14443_3b] = "iso14443-3b",
        [NfcProtocolIso14443_4a] = "iso14443-4a",
        [NfcProtocolIso14443_4b] = "iso14443-4b",
        [NfcProtocolIso15693_3] = "iso15693-3",
        [NfcProtocolFelica] = "felica",
        [NfcProtocolMfUltralight] = "mifare-ultralight",
        [NfcProtocolMfClassic] = "mifare-classic",
        [NfcProtocolMfPlus] = "mifare-plus",
        [NfcProtocolMfDesfire] = "mifare-desfire",
        [NfcProtocolSlix] = "slix",
        [NfcProtocolSt25tb] = "st25tb",
    };
    return protocol < NfcProtocolNum ? names[protocol] : NULL;
}
