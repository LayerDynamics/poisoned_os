#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nfc/protocols/nfc_protocol.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PoisonNfcSession PoisonNfcSession;

typedef struct {
    NfcProtocol protocols[NfcProtocolNum];
    size_t protocol_count;
} PoisonNfcDetection;

PoisonNfcSession* poison_nfc_session_alloc(void);
bool poison_nfc_session_start(PoisonNfcSession* session);
bool poison_nfc_session_wait(
    PoisonNfcSession* session,
    uint32_t timeout_ms,
    PoisonNfcDetection* detection);
void poison_nfc_session_stop(PoisonNfcSession* session);
void poison_nfc_session_free(PoisonNfcSession* session);
const char* poison_nfc_protocol_name(NfcProtocol protocol);

#ifdef __cplusplus
}
#endif
