#pragma once

#include "poison_policy.h"
#include "rpc_poison_crypto.h"

#include <poison_session.pb.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_SESSION_KEY_BYTES          (32u)
#define POISON_SESSION_AUTH_TAG_BYTES     (16u)
#define POISON_SESSION_CHANNEL_MAX        (32u)
#define POISON_SESSION_PAYLOAD_MAX        (1024u)
#define POISON_SESSION_RESUME_TOKEN_BYTES (32u)

typedef enum {
    PoisonSessionUnpaired,
    PoisonSessionNegotiating,
    PoisonSessionConfirming,
    PoisonSessionActive,
    PoisonSessionClosing,
    PoisonSessionRevoked,
} PoisonSessionState;

typedef enum {
    PoisonSessionResultOk,
    PoisonSessionResultInvalid,
    PoisonSessionResultState,
    PoisonSessionResultReplay,
    PoisonSessionResultGap,
    PoisonSessionResultSequenceWrap,
} PoisonSessionResult;

typedef struct PoisonSession {
    PoisonSessionState state;
    uint32_t protocol_version;
    uint64_t session_id;
    uint64_t next_tx_sequence;
    uint64_t next_rx_sequence;
    bool physical_confirmation;
    uint8_t receive_key[POISON_SESSION_KEY_BYTES];
    uint8_t transmit_key[POISON_SESSION_KEY_BYTES];
    bool directional_keys_set;
    PoisonRole role;
    PoisonCapability granted_capabilities;
    uint8_t client_identity_digest[POISON_CRYPTO_SHA256_BYTES];
} PoisonSession;

typedef struct {
    PoisonSession session;
    uint8_t device_private_key[POISON_CRYPTO_P256_PRIVATE_BYTES];
    uint8_t device_public_key[POISON_CRYPTO_P256_PUBLIC_BYTES];
    uint8_t client_public_key[POISON_CRYPTO_P256_PUBLIC_BYTES];
    uint8_t client_identity_public_key[POISON_CRYPTO_P256_PUBLIC_BYTES];
    uint8_t client_identity_digest[POISON_CRYPTO_SHA256_BYTES];
    uint8_t client_nonce[32u];
    uint8_t device_nonce[32u];
    uint8_t transcript_digest[POISON_CRYPTO_SHA256_BYTES];
    char confirmation_code[13u];
    char client_name[33u];
    PoisonRole requested_role;
    PoisonCapability requested_capabilities;
    uint64_t expires_at_ms;
    bool pending;
} PoisonPairingHandshake;

typedef struct {
    bool active;
    uint64_t expires_at_ms;
    uint8_t token_digest[POISON_CRYPTO_SHA256_BYTES];
    PoisonSession session;
} PoisonSessionResumeSlot;

void poison_session_init(PoisonSession* session);

bool poison_pairing_begin(
    PoisonPairingHandshake* handshake,
    const PB_Poison_PairingHello* hello,
    uint64_t now_ms,
    PB_Poison_PairingChallenge* challenge);

bool poison_pairing_complete(
    PoisonPairingHandshake* handshake,
    const PB_Poison_PairingConfirm* confirm,
    uint64_t now_ms,
    bool device_physical_confirmation);

PoisonSessionResult
    poison_session_begin_negotiation(PoisonSession* session, uint32_t protocol_version);

PoisonSessionResult poison_session_begin_confirmation(PoisonSession* session, uint64_t session_id);

PoisonSessionResult poison_session_confirm(PoisonSession* session, bool physical_confirmation);

PoisonSessionResult poison_session_activate(PoisonSession* session);

PoisonSessionResult poison_session_set_authentication_key(
    PoisonSession* session,
    const uint8_t key[POISON_SESSION_KEY_BYTES]);

PoisonSessionResult poison_session_set_directional_keys(
    PoisonSession* session,
    const uint8_t receive_key[POISON_SESSION_KEY_BYTES],
    const uint8_t transmit_key[POISON_SESSION_KEY_BYTES]);

PoisonSessionResult poison_session_encrypt_tx(
    PoisonSession* session,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* plaintext,
    size_t plaintext_length,
    uint64_t* sequence,
    uint8_t* ciphertext,
    uint8_t authentication_tag[POISON_SESSION_AUTH_TAG_BYTES]);

PoisonSessionResult poison_session_decrypt_rx(
    PoisonSession* session,
    uint64_t sequence,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* ciphertext,
    size_t ciphertext_length,
    const uint8_t authentication_tag[POISON_SESSION_AUTH_TAG_BYTES],
    uint8_t* plaintext);

PoisonSessionResult poison_session_authenticate_rx(
    PoisonSession* session,
    uint32_t protocol_version,
    uint64_t sequence,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* payload,
    size_t payload_length,
    const uint8_t authentication_tag[POISON_SESSION_AUTH_TAG_BYTES]);

PoisonSessionResult poison_session_sign_frame(
    const PoisonSession* session,
    uint32_t protocol_version,
    uint64_t sequence,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* payload,
    size_t payload_length,
    uint8_t authentication_tag[POISON_SESSION_AUTH_TAG_BYTES]);

PoisonSessionResult poison_session_reserve_tx(PoisonSession* session, uint64_t* sequence);

PoisonSessionResult poison_session_accept_rx(PoisonSession* session, uint64_t sequence);

PoisonSessionResult poison_session_close(PoisonSession* session);

PoisonSessionResult poison_session_revoke(PoisonSession* session);

void poison_session_resume_slot_init(PoisonSessionResumeSlot* slot);

bool poison_session_resume_store(
    PoisonSessionResumeSlot* slot,
    const PoisonSession* session,
    const uint8_t token[POISON_SESSION_RESUME_TOKEN_BYTES],
    uint64_t now_ms,
    uint64_t ttl_ms);

bool poison_session_resume_take(
    PoisonSessionResumeSlot* slot,
    uint64_t session_id,
    const uint8_t token[POISON_SESSION_RESUME_TOKEN_BYTES],
    uint64_t last_received_sequence,
    uint64_t now_ms,
    PoisonSession* resumed_session);

void poison_session_resume_revoke(PoisonSessionResumeSlot* slot);

#ifdef __cplusplus
}
#endif
