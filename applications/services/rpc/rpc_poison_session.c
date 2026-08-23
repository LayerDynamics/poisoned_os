#include "rpc_poison_session.h"
#include "rpc_poison_crypto.h"
#include "rpc_poison_content_update.h"
#include "poison_pairing_store.h"
#include "../poison_packages/poison_content_update_internal.h"
#include "rpc_i.h"
#include "../poison_packages/poison_package_manager.h"
#include "../poison_audit/poison_audit.h"
#include "../poison_diagnostics/poison_diagnostics.h"

#include <limits.h>
#include <furi_hal.h>
#include <furi_hal_random.h>
#include <furi_hal_version.h>
#include <loader/firmware_api/firmware_api.h>
#include <toolbox/version.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <storage/storage.h>
#include <update_util/update_operation.h>
#include <stdio.h>
#include <string.h>

#define POISON_PAIRING_TTL_MS (60000u)
#define POISON_RESUME_ERROR   "resume-refused"

static void poison_session_encode_u64(uint8_t output[8], uint64_t value);
static void poison_session_encode_u32(uint8_t output[4], uint32_t value);

void poison_session_init(PoisonSession* session) {
    if(!session) return;
    memset(session, 0, sizeof(*session));
    session->state = PoisonSessionUnpaired;
}

static bool poison_session_nonzero(const uint8_t* value, size_t length) {
    if(!value) return false;
    uint8_t aggregate = 0u;
    for(size_t index = 0; index < length; ++index)
        aggregate |= value[index];
    return aggregate != 0u;
}

static bool poison_pairing_append(
    uint8_t* transcript,
    size_t capacity,
    size_t* length,
    const void* value,
    size_t value_length) {
    if(!transcript || !length || !value || value_length > capacity - *length) return false;
    memcpy(transcript + *length, value, value_length);
    *length += value_length;
    return true;
}

static bool poison_pairing_transcript(
    const PB_Poison_PairingHello* hello,
    const PoisonPairingHandshake* handshake,
    uint8_t digest[POISON_CRYPTO_SHA256_BYTES]) {
    uint8_t transcript[384u];
    size_t length = 0u;
    uint8_t encoded_u32[4u];
    uint8_t encoded_u64[8u];
    const size_t name_length = strlen(hello->client_name) + 1u;
    poison_session_encode_u32(encoded_u32, hello->protocol_version);
    bool ok = poison_pairing_append(
        transcript, sizeof(transcript), &length, encoded_u32, sizeof(encoded_u32));
    ok = ok && poison_pairing_append(
                   transcript,
                   sizeof(transcript),
                   &length,
                   hello->client_ephemeral_public_key.bytes,
                   hello->client_ephemeral_public_key.size);
    ok = ok && poison_pairing_append(
                   transcript,
                   sizeof(transcript),
                   &length,
                   hello->client_identity_public_key.bytes,
                   hello->client_identity_public_key.size);
    ok = ok && poison_pairing_append(
                   transcript, sizeof(transcript), &length, hello->client_name, name_length);
    poison_session_encode_u32(encoded_u32, hello->requested_role);
    ok = ok && poison_pairing_append(
                   transcript, sizeof(transcript), &length, encoded_u32, sizeof(encoded_u32));
    poison_session_encode_u32(encoded_u32, hello->requested_capabilities);
    ok = ok && poison_pairing_append(
                   transcript, sizeof(transcript), &length, encoded_u32, sizeof(encoded_u32));
    ok = ok && poison_pairing_append(
                   transcript,
                   sizeof(transcript),
                   &length,
                   hello->client_nonce.bytes,
                   hello->client_nonce.size);
    ok = ok && poison_pairing_append(
                   transcript,
                   sizeof(transcript),
                   &length,
                   handshake->device_public_key,
                   sizeof(handshake->device_public_key));
    ok = ok && poison_pairing_append(
                   transcript,
                   sizeof(transcript),
                   &length,
                   handshake->device_nonce,
                   sizeof(handshake->device_nonce));
    poison_session_encode_u64(encoded_u64, handshake->expires_at_ms);
    ok = ok && poison_pairing_append(
                   transcript, sizeof(transcript), &length, encoded_u64, sizeof(encoded_u64));
    poison_session_encode_u64(encoded_u64, handshake->session.session_id);
    ok = ok && poison_pairing_append(
                   transcript, sizeof(transcript), &length, encoded_u64, sizeof(encoded_u64));
    if(!ok) {
        memset(transcript, 0, sizeof(transcript));
        return false;
    }
    const bool hashed = poison_crypto_sha256(transcript, length, digest) == PoisonCryptoResultOk;
    memset(transcript, 0, sizeof(transcript));
    return hashed;
}

bool poison_pairing_begin(
    PoisonPairingHandshake* handshake,
    const PB_Poison_PairingHello* hello,
    uint64_t now_ms,
    PB_Poison_PairingChallenge* challenge) {
    const uint32_t all_capabilities = POISON_CAPABILITY_STATUS | POISON_CAPABILITY_CONTROL |
                                      POISON_CAPABILITY_LAUNCH | POISON_CAPABILITY_FILES |
                                      POISON_CAPABILITY_EVIDENCE | POISON_CAPABILITY_RADIO |
                                      POISON_CAPABILITY_NATIVE | POISON_CAPABILITY_DESTRUCTIVE;
    if(!handshake || !hello || !challenge || hello->protocol_version != 2u ||
       hello->client_ephemeral_public_key.size != POISON_CRYPTO_P256_PUBLIC_BYTES ||
       hello->client_ephemeral_public_key.bytes[0] != 0x04u || hello->client_nonce.size != 32u ||
       hello->client_identity_public_key.size != POISON_CRYPTO_P256_PUBLIC_BYTES ||
       hello->client_identity_public_key.bytes[0] != 0x04u ||
       !poison_session_nonzero(hello->client_nonce.bytes, hello->client_nonce.size) ||
       hello->client_name[0] == '\0' || strnlen(hello->client_name, 33u) > 32u ||
       hello->requested_role >= PoisonRoleCount ||
       (hello->requested_capabilities & ~all_capabilities) != 0u ||
       UINT64_MAX - now_ms < POISON_PAIRING_TTL_MS) {
        return false;
    }

    memset(handshake, 0, sizeof(*handshake));
    poison_session_init(&handshake->session);
    if(poison_session_begin_negotiation(&handshake->session, 2u) != PoisonSessionResultOk ||
       poison_crypto_generate_p256_keypair(
           handshake->device_private_key, handshake->device_public_key) != PoisonCryptoResultOk) {
        return false;
    }
    memcpy(
        handshake->client_public_key,
        hello->client_ephemeral_public_key.bytes,
        sizeof(handshake->client_public_key));
    memcpy(
        handshake->client_identity_public_key,
        hello->client_identity_public_key.bytes,
        sizeof(handshake->client_identity_public_key));
    if(poison_crypto_sha256(
           handshake->client_identity_public_key,
           sizeof(handshake->client_identity_public_key),
           handshake->client_identity_digest) != PoisonCryptoResultOk) {
        poison_session_revoke(&handshake->session);
        memset(handshake->device_private_key, 0, sizeof(handshake->device_private_key));
        return false;
    }
    memcpy(handshake->client_nonce, hello->client_nonce.bytes, sizeof(handshake->client_nonce));
    furi_hal_random_fill_buf(handshake->device_nonce, sizeof(handshake->device_nonce));
    furi_hal_random_fill_buf(
        (uint8_t*)&handshake->session.session_id, sizeof(handshake->session.session_id));
    if(handshake->session.session_id == 0u) handshake->session.session_id = 1u;
    handshake->expires_at_ms = now_ms + POISON_PAIRING_TTL_MS;
    handshake->requested_role = (PoisonRole)hello->requested_role;
    handshake->requested_capabilities = hello->requested_capabilities;
    memcpy(handshake->client_name, hello->client_name, strlen(hello->client_name) + 1u);
    if(poison_session_begin_confirmation(&handshake->session, handshake->session.session_id) !=
           PoisonSessionResultOk ||
       !poison_pairing_transcript(hello, handshake, handshake->transcript_digest)) {
        poison_session_revoke(&handshake->session);
        memset(handshake->device_private_key, 0, sizeof(handshake->device_private_key));
        return false;
    }
    uint32_t code_value = ((uint32_t)handshake->transcript_digest[0] << 24u) |
                          ((uint32_t)handshake->transcript_digest[1] << 16u) |
                          ((uint32_t)handshake->transcript_digest[2] << 8u) |
                          handshake->transcript_digest[3];
    snprintf(
        handshake->confirmation_code,
        sizeof(handshake->confirmation_code),
        "%06lu",
        (unsigned long)(code_value % 1000000u));
    handshake->pending = true;

    *challenge = (PB_Poison_PairingChallenge)PB_Poison_PairingChallenge_init_zero;
    challenge->protocol_version = 2u;
    challenge->device_ephemeral_public_key.size = sizeof(handshake->device_public_key);
    memcpy(
        challenge->device_ephemeral_public_key.bytes,
        handshake->device_public_key,
        sizeof(handshake->device_public_key));
    challenge->device_nonce.size = sizeof(handshake->device_nonce);
    memcpy(
        challenge->device_nonce.bytes, handshake->device_nonce, sizeof(handshake->device_nonce));
    memcpy(
        challenge->confirmation_code,
        handshake->confirmation_code,
        strlen(handshake->confirmation_code) + 1u);
    challenge->transcript_digest.size = sizeof(handshake->transcript_digest);
    memcpy(
        challenge->transcript_digest.bytes,
        handshake->transcript_digest,
        sizeof(handshake->transcript_digest));
    challenge->expires_at_ms = handshake->expires_at_ms;
    challenge->session_id = handshake->session.session_id;
    return true;
}

bool poison_pairing_complete(
    PoisonPairingHandshake* handshake,
    const PB_Poison_PairingConfirm* confirm,
    uint64_t now_ms,
    bool device_physical_confirmation) {
    if(!handshake || !confirm || !handshake->pending || now_ms >= handshake->expires_at_ms ||
       confirm->transcript_digest.size != sizeof(handshake->transcript_digest) ||
       memcmp(
           confirm->transcript_digest.bytes,
           handshake->transcript_digest,
           sizeof(handshake->transcript_digest)) != 0 ||
       strcmp(confirm->confirmation_code, handshake->confirmation_code) != 0 ||
       confirm->client_identity_signature.size == 0u ||
       poison_crypto_verify_p256_sha256(
           handshake->client_identity_public_key,
           handshake->transcript_digest,
           confirm->client_identity_signature.bytes,
           confirm->client_identity_signature.size) != PoisonCryptoResultOk ||
       !confirm->physical_confirmation || !device_physical_confirmation) {
        if(handshake) {
            poison_session_revoke(&handshake->session);
            memset(handshake->device_private_key, 0, sizeof(handshake->device_private_key));
            handshake->pending = false;
        }
        return false;
    }
    uint8_t shared_secret[POISON_CRYPTO_SHARED_SECRET_BYTES];
    uint8_t salt[64u];
    uint8_t info[45u];
    uint8_t directional_keys[POISON_SESSION_KEY_BYTES * 2u];
    const PoisonPolicyDecision policy = poison_policy_evaluate(
        handshake->requested_role, handshake->requested_capabilities, false, true, 1u);
    memcpy(salt, handshake->client_nonce, sizeof(handshake->client_nonce));
    memcpy(
        salt + sizeof(handshake->client_nonce),
        handshake->device_nonce,
        sizeof(handshake->device_nonce));
    memcpy(info, "poison-rpc-v2", 13u);
    memcpy(info + 13u, handshake->transcript_digest, sizeof(handshake->transcript_digest));
    bool ok = policy.allowed &&
              poison_crypto_p256_shared_secret(
                  handshake->device_private_key, handshake->client_public_key, shared_secret) ==
                  PoisonCryptoResultOk;
    if(ok) {
        ok = poison_crypto_hkdf_sha256(
                 salt,
                 sizeof(salt),
                 shared_secret,
                 sizeof(shared_secret),
                 info,
                 sizeof(info),
                 directional_keys,
                 sizeof(directional_keys)) == PoisonCryptoResultOk;
    }
    if(ok) {
        ok = poison_session_set_directional_keys(
                 &handshake->session,
                 directional_keys,
                 directional_keys + POISON_SESSION_KEY_BYTES) == PoisonSessionResultOk;
    }
    if(ok) {
        ok = poison_session_confirm(&handshake->session, true) == PoisonSessionResultOk &&
             poison_session_activate(&handshake->session) == PoisonSessionResultOk;
    }
    if(ok) {
        handshake->session.role = handshake->requested_role;
        handshake->session.granted_capabilities = policy.granted;
        memcpy(
            handshake->session.client_identity_digest,
            handshake->client_identity_digest,
            sizeof(handshake->session.client_identity_digest));
    }
    memset(shared_secret, 0, sizeof(shared_secret));
    memset(directional_keys, 0, sizeof(directional_keys));
    memset(handshake->device_private_key, 0, sizeof(handshake->device_private_key));
    handshake->pending = false;
    if(!ok) poison_session_revoke(&handshake->session);
    return ok;
}

static bool poison_session_valid_frame(
    uint32_t protocol_version,
    const char* channel,
    const uint8_t* payload,
    size_t payload_length,
    const uint8_t* authentication_tag) {
    return protocol_version == 2u && channel && channel[0] != '\0' &&
           strnlen(channel, POISON_SESSION_CHANNEL_MAX + 1u) <= POISON_SESSION_CHANNEL_MAX &&
           (payload_length == 0 || payload) && payload_length <= POISON_SESSION_PAYLOAD_MAX &&
           authentication_tag;
}

static void poison_session_mac_update_u64(mbedtls_md_context_t* context, uint64_t value) {
    uint8_t encoded[8];
    for(size_t index = 0; index < sizeof(encoded); ++index) {
        encoded[sizeof(encoded) - index - 1u] = (uint8_t)(value >> (index * 8u));
    }
    (void)mbedtls_md_hmac_update(context, encoded, sizeof(encoded));
}

static void poison_session_mac_update_u32(mbedtls_md_context_t* context, uint32_t value) {
    uint8_t encoded[4];
    for(size_t index = 0; index < sizeof(encoded); ++index) {
        encoded[sizeof(encoded) - index - 1u] = (uint8_t)(value >> (index * 8u));
    }
    (void)mbedtls_md_hmac_update(context, encoded, sizeof(encoded));
}

static bool
    poison_session_constant_time_equal(const uint8_t* left, const uint8_t* right, size_t length) {
    uint8_t difference = 0;
    for(size_t index = 0; index < length; ++index)
        difference |= left[index] ^ right[index];
    return difference == 0;
}

static bool poison_session_compute_tag(
    const PoisonSession* session,
    uint32_t protocol_version,
    uint64_t sequence,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* payload,
    size_t payload_length,
    uint8_t tag[POISON_SESSION_AUTH_TAG_BYTES]) {
    const mbedtls_md_info_t* digest = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if(!digest || !session || !tag) return false;
    mbedtls_md_context_t context;
    uint8_t full_tag[32];
    mbedtls_md_init(&context);
    bool ok = mbedtls_md_setup(&context, digest, 1) == 0;
    if(ok)
        ok = mbedtls_md_hmac_starts(
                 &context, session->transmit_key, sizeof(session->transmit_key)) == 0;
    if(ok) poison_session_mac_update_u32(&context, protocol_version);
    if(ok) poison_session_mac_update_u64(&context, session->session_id);
    if(ok) poison_session_mac_update_u64(&context, sequence);
    if(ok) poison_session_mac_update_u64(&context, acknowledgement);
    if(ok)
        ok = mbedtls_md_hmac_update(&context, (const uint8_t*)channel, strlen(channel) + 1u) == 0;
    if(ok) ok = mbedtls_md_hmac_update(&context, payload, payload_length) == 0;
    if(ok) ok = mbedtls_md_hmac_finish(&context, full_tag) == 0;
    mbedtls_md_free(&context);
    if(ok) memcpy(tag, full_tag, POISON_SESSION_AUTH_TAG_BYTES);
    memset(full_tag, 0, sizeof(full_tag));
    return ok;
}

PoisonSessionResult
    poison_session_begin_negotiation(PoisonSession* session, uint32_t protocol_version) {
    if(!session || protocol_version != 2u) return PoisonSessionResultInvalid;
    if(session->state != PoisonSessionUnpaired) return PoisonSessionResultState;
    session->protocol_version = protocol_version;
    session->state = PoisonSessionNegotiating;
    return PoisonSessionResultOk;
}

PoisonSessionResult
    poison_session_begin_confirmation(PoisonSession* session, uint64_t session_id) {
    if(!session || session_id == 0) return PoisonSessionResultInvalid;
    if(session->state != PoisonSessionNegotiating) return PoisonSessionResultState;
    session->session_id = session_id;
    session->state = PoisonSessionConfirming;
    return PoisonSessionResultOk;
}

PoisonSessionResult poison_session_confirm(PoisonSession* session, bool physical_confirmation) {
    if(!session) return PoisonSessionResultInvalid;
    if(session->state != PoisonSessionConfirming) return PoisonSessionResultState;
    if(!physical_confirmation) return PoisonSessionResultInvalid;
    session->physical_confirmation = true;
    return PoisonSessionResultOk;
}

PoisonSessionResult poison_session_activate(PoisonSession* session) {
    if(!session) return PoisonSessionResultInvalid;
    if(session->state != PoisonSessionConfirming || !session->physical_confirmation) {
        return PoisonSessionResultState;
    }
    session->next_tx_sequence = 0;
    session->next_rx_sequence = 0;
    session->state = PoisonSessionActive;
    return PoisonSessionResultOk;
}

PoisonSessionResult poison_session_set_authentication_key(
    PoisonSession* session,
    const uint8_t key[POISON_SESSION_KEY_BYTES]) {
    return poison_session_set_directional_keys(session, key, key);
}

PoisonSessionResult poison_session_set_directional_keys(
    PoisonSession* session,
    const uint8_t receive_key[POISON_SESSION_KEY_BYTES],
    const uint8_t transmit_key[POISON_SESSION_KEY_BYTES]) {
    if(!session || !receive_key || !transmit_key || session->state != PoisonSessionConfirming) {
        return PoisonSessionResultInvalid;
    }
    memcpy(session->receive_key, receive_key, POISON_SESSION_KEY_BYTES);
    memcpy(session->transmit_key, transmit_key, POISON_SESSION_KEY_BYTES);
    session->directional_keys_set = true;
    return PoisonSessionResultOk;
}

PoisonSessionResult poison_session_authenticate_rx(
    PoisonSession* session,
    uint32_t protocol_version,
    uint64_t sequence,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* payload,
    size_t payload_length,
    const uint8_t authentication_tag[POISON_SESSION_AUTH_TAG_BYTES]) {
    if(!session || session->state != PoisonSessionActive || !session->directional_keys_set ||
       !poison_session_valid_frame(
           protocol_version, channel, payload, payload_length, authentication_tag)) {
        return PoisonSessionResultInvalid;
    }
    uint8_t expected_tag[POISON_SESSION_AUTH_TAG_BYTES];
    PoisonSession receiver = *session;
    memcpy(receiver.transmit_key, session->receive_key, sizeof(receiver.transmit_key));
    if(!poison_session_compute_tag(
           &receiver,
           protocol_version,
           sequence,
           acknowledgement,
           channel,
           payload,
           payload_length,
           expected_tag)) {
        return PoisonSessionResultInvalid;
    }
    bool authenticated =
        poison_session_constant_time_equal(expected_tag, authentication_tag, sizeof(expected_tag));
    memset(expected_tag, 0, sizeof(expected_tag));
    if(!authenticated) return PoisonSessionResultInvalid;
    return poison_session_accept_rx(session, sequence);
}

PoisonSessionResult poison_session_sign_frame(
    const PoisonSession* session,
    uint32_t protocol_version,
    uint64_t sequence,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* payload,
    size_t payload_length,
    uint8_t authentication_tag[POISON_SESSION_AUTH_TAG_BYTES]) {
    if(!session || session->state != PoisonSessionActive || !session->directional_keys_set ||
       !poison_session_valid_frame(
           protocol_version, channel, payload, payload_length, authentication_tag) ||
       !poison_session_compute_tag(
           session,
           protocol_version,
           sequence,
           acknowledgement,
           channel,
           payload,
           payload_length,
           authentication_tag)) {
        return PoisonSessionResultInvalid;
    }
    return PoisonSessionResultOk;
}

static void poison_session_encode_u64(uint8_t output[8], uint64_t value) {
    for(size_t index = 0; index < 8u; ++index) {
        output[7u - index] = (uint8_t)(value >> (index * 8u));
    }
}

static void poison_session_encode_u32(uint8_t output[4], uint32_t value) {
    for(size_t index = 0; index < 4u; ++index) {
        output[3u - index] = (uint8_t)(value >> (index * 8u));
    }
}

static bool poison_session_build_aad(
    const PoisonSession* session,
    uint64_t sequence,
    uint64_t acknowledgement,
    const char* channel,
    uint8_t aad[61],
    size_t* aad_length) {
    if(!session || !channel || !aad || !aad_length) return false;
    const size_t channel_length = strnlen(channel, POISON_SESSION_CHANNEL_MAX + 1u);
    if(channel_length == 0u || channel_length > POISON_SESSION_CHANNEL_MAX) return false;
    poison_session_encode_u32(aad, session->protocol_version);
    poison_session_encode_u64(aad + 4u, session->session_id);
    poison_session_encode_u64(aad + 12u, sequence);
    poison_session_encode_u64(aad + 20u, acknowledgement);
    memcpy(aad + 28u, channel, channel_length + 1u);
    *aad_length = 28u + channel_length + 1u;
    return true;
}

static void poison_session_build_iv(
    const PoisonSession* session,
    uint64_t sequence,
    uint8_t iv[POISON_CRYPTO_GCM_IV_BYTES]) {
    poison_session_encode_u32(iv, (uint32_t)(session->session_id >> 32u));
    poison_session_encode_u64(iv + 4u, sequence);
}

PoisonSessionResult poison_session_encrypt_tx(
    PoisonSession* session,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* plaintext,
    size_t plaintext_length,
    uint64_t* sequence,
    uint8_t* ciphertext,
    uint8_t authentication_tag[POISON_SESSION_AUTH_TAG_BYTES]) {
    if(!session || !sequence || !ciphertext || !authentication_tag ||
       (plaintext_length != 0u && !plaintext) || plaintext_length > POISON_SESSION_PAYLOAD_MAX ||
       session->state != PoisonSessionActive || !session->directional_keys_set) {
        return PoisonSessionResultInvalid;
    }
    PoisonSessionResult result = poison_session_reserve_tx(session, sequence);
    if(result != PoisonSessionResultOk) return result;
    uint8_t aad[61];
    size_t aad_length = 0u;
    uint8_t iv[POISON_CRYPTO_GCM_IV_BYTES];
    if(!poison_session_build_aad(session, *sequence, acknowledgement, channel, aad, &aad_length)) {
        return PoisonSessionResultInvalid;
    }
    poison_session_build_iv(session, *sequence, iv);
    return poison_crypto_gcm_encrypt(
               session->transmit_key,
               iv,
               aad,
               aad_length,
               plaintext,
               ciphertext,
               plaintext_length,
               authentication_tag) == PoisonCryptoResultOk ?
               PoisonSessionResultOk :
               PoisonSessionResultInvalid;
}

PoisonSessionResult poison_session_decrypt_rx(
    PoisonSession* session,
    uint64_t sequence,
    uint64_t acknowledgement,
    const char* channel,
    const uint8_t* ciphertext,
    size_t ciphertext_length,
    const uint8_t authentication_tag[POISON_SESSION_AUTH_TAG_BYTES],
    uint8_t* plaintext) {
    if(!session || !authentication_tag || !plaintext || (ciphertext_length != 0u && !ciphertext) ||
       ciphertext_length > POISON_SESSION_PAYLOAD_MAX || session->state != PoisonSessionActive ||
       !session->directional_keys_set) {
        return PoisonSessionResultInvalid;
    }
    if(sequence < session->next_rx_sequence) return PoisonSessionResultReplay;
    if(sequence > session->next_rx_sequence) return PoisonSessionResultGap;
    uint8_t aad[61];
    size_t aad_length = 0u;
    uint8_t iv[POISON_CRYPTO_GCM_IV_BYTES];
    if(!poison_session_build_aad(session, sequence, acknowledgement, channel, aad, &aad_length)) {
        return PoisonSessionResultInvalid;
    }
    poison_session_build_iv(session, sequence, iv);
    if(poison_crypto_gcm_decrypt(
           session->receive_key,
           iv,
           aad,
           aad_length,
           ciphertext,
           plaintext,
           ciphertext_length,
           authentication_tag) != PoisonCryptoResultOk) {
        return PoisonSessionResultInvalid;
    }
    return poison_session_accept_rx(session, sequence);
}

PoisonSessionResult poison_session_reserve_tx(PoisonSession* session, uint64_t* sequence) {
    if(!session || !sequence) return PoisonSessionResultInvalid;
    if(session->state != PoisonSessionActive) return PoisonSessionResultState;
    if(session->next_tx_sequence == UINT64_MAX) return PoisonSessionResultSequenceWrap;
    *sequence = session->next_tx_sequence++;
    return PoisonSessionResultOk;
}

PoisonSessionResult poison_session_accept_rx(PoisonSession* session, uint64_t sequence) {
    if(!session) return PoisonSessionResultInvalid;
    if(session->state != PoisonSessionActive) return PoisonSessionResultState;
    if(sequence < session->next_rx_sequence) return PoisonSessionResultReplay;
    if(sequence > session->next_rx_sequence) return PoisonSessionResultGap;
    if(session->next_rx_sequence == UINT64_MAX) return PoisonSessionResultSequenceWrap;
    ++session->next_rx_sequence;
    return PoisonSessionResultOk;
}

PoisonSessionResult poison_session_close(PoisonSession* session) {
    if(!session) return PoisonSessionResultInvalid;
    if(session->state != PoisonSessionActive) return PoisonSessionResultState;
    session->state = PoisonSessionClosing;
    return PoisonSessionResultOk;
}

PoisonSessionResult poison_session_revoke(PoisonSession* session) {
    if(!session) return PoisonSessionResultInvalid;
    session->state = PoisonSessionRevoked;
    session->physical_confirmation = false;
    session->session_id = 0;
    session->next_tx_sequence = 0;
    session->next_rx_sequence = 0;
    memset(session->receive_key, 0, sizeof(session->receive_key));
    memset(session->transmit_key, 0, sizeof(session->transmit_key));
    session->directional_keys_set = false;
    return PoisonSessionResultOk;
}

void poison_session_resume_slot_init(PoisonSessionResumeSlot* slot) {
    if(slot) memset(slot, 0, sizeof(*slot));
}

void poison_session_resume_revoke(PoisonSessionResumeSlot* slot) {
    if(slot) memset(slot, 0, sizeof(*slot));
}

bool poison_session_resume_store(
    PoisonSessionResumeSlot* slot,
    const PoisonSession* session,
    const uint8_t token[POISON_SESSION_RESUME_TOKEN_BYTES],
    uint64_t now_ms,
    uint64_t ttl_ms) {
    if(!slot || !session || !token || session->state != PoisonSessionActive ||
       !session->directional_keys_set || session->session_id == 0u || ttl_ms == 0u ||
       ttl_ms > UINT64_MAX - now_ms ||
       !poison_session_nonzero(token, POISON_SESSION_RESUME_TOKEN_BYTES)) {
        return false;
    }
    PoisonSessionResumeSlot replacement;
    memset(&replacement, 0, sizeof(replacement));
    if(poison_crypto_sha256(token, POISON_SESSION_RESUME_TOKEN_BYTES, replacement.token_digest) !=
       PoisonCryptoResultOk) {
        return false;
    }
    replacement.session = *session;
    replacement.expires_at_ms = now_ms + ttl_ms;
    replacement.active = true;
    poison_session_resume_revoke(slot);
    *slot = replacement;
    memset(&replacement, 0, sizeof(replacement));
    return true;
}

bool poison_session_resume_take(
    PoisonSessionResumeSlot* slot,
    uint64_t session_id,
    const uint8_t token[POISON_SESSION_RESUME_TOKEN_BYTES],
    uint64_t last_received_sequence,
    uint64_t now_ms,
    PoisonSession* resumed_session) {
    if(!slot || !resumed_session || !token || !slot->active) return false;
    if(now_ms >= slot->expires_at_ms) {
        poison_session_resume_revoke(slot);
        return false;
    }
    if(session_id == 0u || slot->session.session_id != session_id ||
       slot->session.state != PoisonSessionActive || !slot->session.directional_keys_set) {
        return false;
    }
    const bool sequence_matches =
        (slot->session.next_tx_sequence == 0u && last_received_sequence == UINT64_MAX) ||
        (last_received_sequence != UINT64_MAX &&
         last_received_sequence + 1u == slot->session.next_tx_sequence);
    if(!sequence_matches) return false;
    uint8_t token_digest[POISON_CRYPTO_SHA256_BYTES];
    if(poison_crypto_sha256(token, POISON_SESSION_RESUME_TOKEN_BYTES, token_digest) !=
       PoisonCryptoResultOk) {
        return false;
    }
    const bool token_matches =
        poison_session_constant_time_equal(slot->token_digest, token_digest, sizeof(token_digest));
    memset(token_digest, 0, sizeof(token_digest));
    if(!token_matches) return false;
    *resumed_session = slot->session;
    poison_session_resume_revoke(slot);
    return true;
}

typedef struct {
    RpcSession* rpc_session;
    PoisonPairingHandshake handshake;
    RpcPoisonContentUpdate* content_update;
    uint8_t resume_token[POISON_SESSION_RESUME_TOKEN_BYTES];
    bool resume_issued;
    bool pairing_store_ready;
    bool trusted_identity;
    bool identity_session_counted;
} RpcPoisonSystem;

static bool rpc_poison_count_identity_session(RpcPoisonSystem* system) {
    if(!system || system->identity_session_counted ||
       !poison_pairing_registry_session_open(system->handshake.session.client_identity_digest)) {
        return false;
    }
    system->identity_session_counted = true;
    return true;
}

static bool rpc_poison_identity_allowed(const RpcPoisonSystem* system) {
    if(!system || !system->pairing_store_ready) return false;
    PoisonPairingRecord record;
    return poison_pairing_registry_find(
        system->handshake.session.client_identity_digest, &record, NULL);
}

static bool rpc_poison_random_resume_token(uint8_t token[POISON_SESSION_RESUME_TOKEN_BYTES]) {
    if(!token) return false;
    for(size_t attempt = 0u; attempt < 4u; ++attempt) {
        furi_hal_random_fill_buf(token, POISON_SESSION_RESUME_TOKEN_BYTES);
        if(poison_session_nonzero(token, POISON_SESSION_RESUME_TOKEN_BYTES)) return true;
    }
    memset(token, 0, POISON_SESSION_RESUME_TOKEN_BYTES);
    return false;
}

static void rpc_poison_resume_request_process(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(context);
    RpcPoisonSystem* system = context;
    PB_Main response = PB_Main_init_zero;
    response.command_id = request->command_id;
    response.command_status = PB_CommandStatus_OK;
    response.which_content = PB_Main_poison_resume_response_tag;
    PB_Poison_ResumeResponse* resume_response = &response.content.poison_resume_response;
    if(request->which_content != PB_Main_poison_resume_request_tag || request->has_next) {
        strcpy(resume_response->error, POISON_RESUME_ERROR);
        rpc_send_and_release(system->rpc_session, &response);
        return;
    }

    const PB_Poison_ResumeRequest* resume_request = &request->content.poison_resume_request;
    if(rpc_session_is_secure_dispatch_active(system->rpc_session)) {
        uint64_t active_session_id = 0u;
        PoisonRole role = PoisonRoleObserver;
        const bool valid =
            rpc_poison_identity_allowed(system) && resume_request->resume_token.size == 0u &&
            rpc_session_get_secure_identity(system->rpc_session, &active_session_id, &role) &&
            active_session_id == resume_request->session_id &&
            system->handshake.session.next_tx_sequence < UINT64_MAX &&
            rpc_poison_random_resume_token(system->resume_token);
        UNUSED(role);
        if(valid) {
            system->resume_issued = true;
            resume_response->accepted = true;
            resume_response->resume_token.size = sizeof(system->resume_token);
            memcpy(
                resume_response->resume_token.bytes,
                system->resume_token,
                sizeof(system->resume_token));
            resume_response->next_sequence = system->handshake.session.next_tx_sequence + 1u;
        } else {
            strcpy(resume_response->error, POISON_RESUME_ERROR);
        }
        rpc_send_and_release(system->rpc_session, &response);
        return;
    }

    uint8_t rotated_token[POISON_SESSION_RESUME_TOKEN_BYTES];
    const bool token_ready = rpc_poison_random_resume_token(rotated_token);
    bool resumed = token_ready &&
                   resume_request->resume_token.size == POISON_SESSION_RESUME_TOKEN_BYTES &&
                   rpc_session_take_resume(
                       system->rpc_session,
                       resume_request->session_id,
                       resume_request->resume_token.bytes,
                       resume_request->last_received_sequence,
                       &system->handshake.session);
    if(resumed && !rpc_poison_identity_allowed(system)) {
        poison_session_revoke(&system->handshake.session);
        resumed = false;
    }
    if(resumed) {
        if(!rpc_poison_count_identity_session(system)) {
            poison_session_revoke(&system->handshake.session);
            resumed = false;
        }
    }
    if(resumed) {
        memcpy(system->resume_token, rotated_token, sizeof(system->resume_token));
        system->resume_issued = true;
        resume_response->accepted = true;
        resume_response->resume_token.size = sizeof(system->resume_token);
        memcpy(
            resume_response->resume_token.bytes,
            system->resume_token,
            sizeof(system->resume_token));
        resume_response->next_sequence = system->handshake.session.next_tx_sequence;
    } else {
        strcpy(resume_response->error, POISON_RESUME_ERROR);
    }
    memset(rotated_token, 0, sizeof(rotated_token));
    rpc_send_and_release(system->rpc_session, &response);
    if(resumed) {
        rpc_session_activate_secure_transport(system->rpc_session, &system->handshake.session);
    }
}

static uint64_t rpc_poison_now_ms(void) {
    return ((uint64_t)furi_get_tick() * 1000u) / furi_kernel_get_tick_frequency();
}

static bool rpc_poison_prepare_content_update(void* context, const char* manifest_path) {
    RpcPoisonContentUpdate* engine = context;
    if(!engine || update_operation_prepare(manifest_path) != UpdatePrepareResultOK) return false;
    if(poison_content_update_health_arm_at(
           &engine->update, POISON_CONTENT_UPDATE_HEALTH_PENDING_PATH)) {
        return true;
    }
    update_operation_disarm();
    return false;
}

static bool rpc_poison_prepare_content_rollback(void* context, const char* manifest_path) {
    UNUSED(context);
    return update_operation_prepare(manifest_path) == UpdatePrepareResultOK;
}

static void rpc_poison_content_update_reset(void* context, uint32_t arg) {
    UNUSED(context);
    UNUSED(arg);
    furi_delay_ms(100u);
    furi_hal_power_reset();
}

static bool rpc_poison_verify_content_update(
    void* context,
    const char* manifest_path,
    const char* candidate_digest,
    PoisonPackageVerifiedArchive* verified) {
    RpcPoisonContentUpdate* engine = context;
    if(!engine || !manifest_path || !candidate_digest || !verified) return false;
    const Version* current_firmware = furi_hal_version_get_firmware_version();
    const char* installed_version = current_firmware ? version_get_version(current_firmware) :
                                                       NULL;
    const PoisonPackageArchiveResult result = poison_package_verify_archive(
        manifest_path,
        candidate_digest,
        poison_content_update_authorities(),
        firmware_api_interface->api_version_major,
        firmware_api_interface->api_version_minor,
        installed_version,
        verified);
    if(result != PoisonPackageArchiveOk || strcmp(verified->content_type, "firmware") != 0 ||
       strcmp(verified->entrypoint, "update.fuf") != 0) {
        memset(verified, 0, sizeof(*verified));
        return false;
    }
    if(engine->active && engine->update.state == PoisonContentUpdateStaged) {
        char destination[POISON_CONTENT_UPDATE_MANIFEST_PATH_BYTES];
        if(snprintf(
               destination,
               sizeof(destination),
               "/ext/update/.poison-verified/%s/%s",
               verified->package_id,
               verified->version) >= (int)sizeof(destination) ||
           !poison_package_extract_verified_archive(manifest_path, verified, destination) ||
           snprintf(
               engine->activation_manifest_path,
               sizeof(engine->activation_manifest_path),
               "%s/%s",
               destination,
               verified->entrypoint) >= (int)sizeof(engine->activation_manifest_path)) {
            engine->activation_manifest_path[0] = '\0';
            memset(verified, 0, sizeof(*verified));
            return false;
        }
    }
    return true;
}

static void rpc_poison_fingerprint(
    const uint8_t digest[POISON_CRYPTO_SHA256_BYTES],
    char fingerprint[17u]) {
    static const char hex[] = "0123456789abcdef";
    for(size_t index = 0; index < 8u; ++index) {
        fingerprint[index * 2u] = hex[digest[index] >> 4u];
        fingerprint[index * 2u + 1u] = hex[digest[index] & 0x0fu];
    }
    fingerprint[16u] = '\0';
}

static void rpc_poison_pairing_audit(
    const PoisonPairingHandshake* handshake,
    bool returning_client,
    bool allowed) {
    if(!handshake) return;
    PoisonAuditEvent event;
    char metadata[POISON_AUDIT_METADATA_MAX + 1u];
    snprintf(
        metadata,
        sizeof(metadata),
        "name=%.24s;role=%lu;returning=%s",
        handshake->client_name,
        (unsigned long)handshake->requested_role,
        returning_client ? "yes" : "no");
    (void)poison_audit_append(
        poison_audit_get(),
        handshake->client_identity_digest,
        "pairing.authenticate",
        "client",
        allowed ? PoisonAuditDecisionAllowed : PoisonAuditDecisionDenied,
        rpc_poison_now_ms(),
        handshake->transcript_digest,
        metadata,
        &event);
    memset(&event, 0, sizeof(event));
}

static void rpc_poison_pairing_hello_process(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(context);
    RpcPoisonSystem* system = context;
    if(request->which_content != PB_Main_poison_pairing_hello_tag || request->has_next ||
       !system->pairing_store_ready) {
        rpc_send_and_release_empty(
            system->rpc_session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    PB_Main response = PB_Main_init_zero;
    response.command_id = request->command_id;
    response.command_status = PB_CommandStatus_OK;
    response.which_content = PB_Main_poison_pairing_challenge_tag;
    if(!poison_pairing_begin(
           &system->handshake,
           &request->content.poison_pairing_hello,
           rpc_poison_now_ms(),
           &response.content.poison_pairing_challenge)) {
        rpc_send_and_release_empty(
            system->rpc_session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    PoisonPairingRecord trusted_record;
    system->trusted_identity =
        poison_pairing_registry_find(
            system->handshake.client_identity_digest, &trusted_record, NULL) &&
        trusted_record.role == system->handshake.requested_role &&
        strcmp(trusted_record.client_name, system->handshake.client_name) == 0;
    response.content.poison_pairing_challenge.identity_trusted = system->trusted_identity;
    rpc_send_and_release(system->rpc_session, &response);
}

static void rpc_poison_pairing_confirm_process(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(context);
    RpcPoisonSystem* system = context;
    if(request->which_content != PB_Main_poison_pairing_confirm_tag || request->has_next ||
       !system->handshake.pending) {
        rpc_send_and_release_empty(
            system->rpc_session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    char fingerprint[17u];
    rpc_poison_fingerprint(system->handshake.client_identity_digest, fingerprint);
    const bool device_approved = system->trusted_identity ||
                                 rpc_session_request_pairing_confirmation(
                                     system->rpc_session,
                                     system->handshake.confirmation_code,
                                     fingerprint,
                                     system->handshake.client_name,
                                     system->handshake.requested_role,
                                     system->handshake.requested_capabilities);
    bool paired = poison_pairing_complete(
        &system->handshake,
        &request->content.poison_pairing_confirm,
        rpc_poison_now_ms(),
        device_approved);
    if(paired && !system->trusted_identity) {
        size_t record_index = 0u;
        paired = poison_pairing_registry_add(
            system->handshake.client_identity_digest,
            system->handshake.client_name,
            system->handshake.requested_role,
            &record_index);
        if(!paired) poison_session_revoke(&system->handshake.session);
    }
    if(paired && !rpc_poison_count_identity_session(system)) {
        poison_session_revoke(&system->handshake.session);
        paired = false;
    }
    rpc_poison_pairing_audit(&system->handshake, system->trusted_identity, paired);
    rpc_send_and_release_empty(
        system->rpc_session,
        request->command_id,
        paired ? PB_CommandStatus_OK : PB_CommandStatus_ERROR_INVALID_PARAMETERS);
    if(paired) {
        rpc_session_activate_secure_transport(system->rpc_session, &system->handshake.session);
    }
}

static void rpc_poison_session_envelope_process(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(context);
    RpcPoisonSystem* system = context;
    if(request->which_content != PB_Main_poison_session_envelope_tag || request->has_next ||
       system->handshake.session.state != PoisonSessionActive) {
        return;
    }
    if(!rpc_poison_identity_allowed(system)) {
        rpc_session_revoke_secure_transport(system->rpc_session);
        return;
    }
    const PB_Poison_SessionEnvelope* envelope = &request->content.poison_session_envelope;
    if(envelope->session_id != system->handshake.session.session_id ||
       envelope->authentication_tag.size != POISON_SESSION_AUTH_TAG_BYTES ||
       envelope->payload.size == 0u) {
        return;
    }
    uint8_t* plaintext = malloc(sizeof(envelope->payload.bytes));
    if(!plaintext) return;
    const PoisonSessionResult result = poison_session_decrypt_rx(
        &system->handshake.session,
        envelope->sequence,
        envelope->acknowledgement,
        envelope->channel,
        envelope->payload.bytes,
        envelope->payload.size,
        envelope->authentication_tag.bytes,
        plaintext);
    if(result == PoisonSessionResultOk) {
        (void)rpc_session_dispatch_secure_payload(
            system->rpc_session,
            &system->handshake.session,
            envelope->channel,
            envelope->sequence,
            plaintext,
            envelope->payload.size);
    }
    memset(plaintext, 0, sizeof(envelope->payload.bytes));
    free(plaintext);
}

static void rpc_poison_diagnostic_fill_counters(
    PB_Poison_DiagnosticCounters* output,
    const PoisonDiagnosticCounters* counters) {
    *output = (PB_Poison_DiagnosticCounters)PB_Poison_DiagnosticCounters_init_zero;
    output->session_established = counters->session_established;
    output->transport_errors = counters->transport_errors;
    output->dropped_frames = counters->dropped_frames;
    output->retried_frames = counters->retried_frames;
    output->command_failures = counters->command_failures;
    output->app_crashes = counters->app_crashes;
    output->policy_denials = counters->policy_denials;
    output->package_verifications = counters->package_verifications;
    output->package_revocations = counters->package_revocations;
    output->update_stages = counters->update_stages;
    output->update_health = counters->update_health;
    output->update_rollbacks = counters->update_rollbacks;
    output->recoveries = counters->recoveries;
    output->javascript_starts = counters->javascript_starts;
    output->javascript_terminals = counters->javascript_terminals;
    output->javascript_crashes = counters->javascript_crashes;
    output->javascript_limits = counters->javascript_limits;
    output->javascript_recoveries = counters->javascript_recoveries;
}

static void rpc_poison_policy_request_process(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(context);
    RpcPoisonSystem* system = context;
    if(request->which_content != PB_Main_poison_policy_request_tag || request->has_next ||
       !rpc_session_is_secure_dispatch_active(system->rpc_session)) {
        rpc_send_and_release_empty(
            system->rpc_session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }

    const PB_Poison_PolicyRequest* policy_request = &request->content.poison_policy_request;
    uint64_t session_id = 0u;
    PoisonRole authenticated_role = PoisonRoleObserver;
    const PoisonCapability known_capabilities =
        POISON_CAPABILITY_STATUS | POISON_CAPABILITY_CONTROL | POISON_CAPABILITY_LAUNCH |
        POISON_CAPABILITY_FILES | POISON_CAPABILITY_EVIDENCE | POISON_CAPABILITY_RADIO |
        POISON_CAPABILITY_NATIVE | POISON_CAPABILITY_DESTRUCTIVE;
    if(policy_request->policy_version != 1u ||
       (uint32_t)policy_request->role >= (uint32_t)PoisonRoleCount ||
       policy_request->requested_capabilities == 0u ||
       (policy_request->requested_capabilities & ~known_capabilities) != 0u ||
       !rpc_session_get_secure_identity(system->rpc_session, &session_id, &authenticated_role) ||
       (uint32_t)authenticated_role != (uint32_t)policy_request->role) {
        rpc_send_and_release_empty(
            system->rpc_session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    (void)session_id;

    const PoisonPolicyDecision decision = poison_policy_evaluate(
        authenticated_role,
        policy_request->requested_capabilities,
        policy_request->device_locked,
        policy_request->physical_confirmation,
        policy_request->policy_version);
    PB_Main response = PB_Main_init_zero;
    response.command_id = request->command_id;
    response.command_status = PB_CommandStatus_OK;
    response.which_content = PB_Main_poison_policy_decision_tag;
    response.content.poison_policy_decision.granted_capabilities = decision.granted;
    response.content.poison_policy_decision.allowed = decision.allowed;
    response.content.poison_policy_decision.policy_version = decision.policy_version;
    if(!decision.allowed) {
        strcpy(
            response.content.poison_policy_decision.denial_reason,
            "requested capabilities are not permitted in the current device state");
    }
    rpc_send_and_release(system->rpc_session, &response);
}

static void rpc_poison_diagnostic_snapshot_request_process(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(context);
    RpcPoisonSystem* system = context;
    if(request->which_content != PB_Main_poison_diagnostic_snapshot_request_tag ||
       request->has_next || !rpc_session_is_secure_dispatch_active(system->rpc_session)) {
        rpc_send_and_release_empty(
            system->rpc_session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    const PB_Poison_DiagnosticSnapshotRequest* snapshot_request =
        &request->content.poison_diagnostic_snapshot_request;
    const size_t limit = snapshot_request->max_events == 0u ? POISON_DIAGNOSTICS_RING_SIZE :
                                                              snapshot_request->max_events;
    if(limit > POISON_DIAGNOSTICS_RING_SIZE) {
        rpc_send_and_release_empty(
            system->rpc_session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }

    PoisonDiagnosticEvent* events =
        malloc(POISON_DIAGNOSTICS_RING_SIZE * sizeof(PoisonDiagnosticEvent));
    if(!events) {
        rpc_send_and_release_empty(
            system->rpc_session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    const PoisonDiagnostics* diagnostics = poison_diagnostics_get();
    size_t event_count = 0u;
    for(size_t index = 0; index < POISON_DIAGNOSTICS_RING_SIZE; ++index) {
        if(diagnostics->events[index].event_id > snapshot_request->after_event_id) {
            events[event_count++] = diagnostics->events[index];
        }
    }
    for(size_t index = 1u; index < event_count; ++index) {
        PoisonDiagnosticEvent value = events[index];
        size_t insertion = index;
        while(insertion > 0u && events[insertion - 1u].event_id > value.event_id) {
            events[insertion] = events[insertion - 1u];
            --insertion;
        }
        events[insertion] = value;
    }
    if(event_count > limit) event_count = limit;

    PB_Main response = PB_Main_init_zero;
    response.command_id = request->command_id;
    response.command_status = PB_CommandStatus_OK;
    response.has_next = event_count > 0u;
    response.which_content = PB_Main_poison_diagnostic_counters_tag;
    rpc_poison_diagnostic_fill_counters(
        &response.content.poison_diagnostic_counters, poison_diagnostics_counters(diagnostics));
    rpc_send_and_release(system->rpc_session, &response);

    for(size_t index = 0u; index < event_count; ++index) {
        response = (PB_Main)PB_Main_init_zero;
        response.command_id = request->command_id;
        response.command_status = PB_CommandStatus_OK;
        response.has_next = index + 1u < event_count;
        response.which_content = PB_Main_poison_diagnostic_event_tag;
        PB_Poison_DiagnosticEvent* output = &response.content.poison_diagnostic_event;
        output->event_id = events[index].event_id;
        const char* category = poison_diagnostics_category_name(events[index].category);
        if(category) strcpy(output->category, category);
        strcpy(output->summary, events[index].summary);
        output->timestamp_ms = events[index].timestamp_ms;
        output->correlation_digest.size = sizeof(events[index].correlation_digest);
        memcpy(
            output->correlation_digest.bytes,
            events[index].correlation_digest,
            sizeof(events[index].correlation_digest));
        rpc_send_and_release(system->rpc_session, &response);
    }
    memset(events, 0, POISON_DIAGNOSTICS_RING_SIZE * sizeof(PoisonDiagnosticEvent));
    free(events);
}

static void rpc_poison_content_update_request_process(const PB_Main* request, void* context) {
    furi_assert(request);
    furi_assert(context);
    RpcPoisonSystem* system = context;
    if(request->which_content != PB_Main_poison_content_update_request_tag || request->has_next ||
       system->handshake.session.state != PoisonSessionActive ||
       !rpc_session_is_secure_dispatch_active(system->rpc_session)) {
        rpc_send_and_release_empty(
            system->rpc_session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    const PB_Poison_ContentUpdateRequest* update_request =
        &request->content.poison_content_update_request;
    RpcPoisonContentUpdateRequestContext request_context = {
        .session_id = system->handshake.session.session_id,
        .role = system->handshake.session.role,
        .policy_version = 1u,
        .now_ms = rpc_poison_now_ms(),
        .physical_confirmed = false,
    };
    bool physical_confirmed = false;
    if(update_request->operation ==
           PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_ACTIVATE &&
       rpc_poison_content_update_confirmation_matches(
           system->content_update, update_request, &request_context)) {
        physical_confirmed = rpc_session_request_content_update_confirmation(
            system->rpc_session,
            update_request->update_id,
            system->content_update->update.candidate_digest,
            system->content_update->update.content_type);
    }
    request_context.now_ms = rpc_poison_now_ms();
    request_context.physical_confirmed = physical_confirmed;
    PB_Main response = PB_Main_init_zero;
    response.command_id = request->command_id;
    response.command_status = PB_CommandStatus_OK;
    response.which_content = PB_Main_poison_content_update_status_tag;
    RpcPoisonContentUpdate* previous = malloc(sizeof(*previous));
    if(!previous) {
        rpc_send_and_release_empty(
            system->rpc_session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    *previous = *system->content_update;
    const bool processed = rpc_poison_content_update_process(
        system->content_update,
        update_request,
        &request_context,
        &response.content.poison_content_update_status);
    const bool persisted =
        processed &&
        (update_request->operation ==
             PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_INSPECT ||
         update_request->operation ==
             PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_HEALTH ||
         rpc_poison_content_update_save(system->content_update, POISON_CONTENT_UPDATE_STATE_PATH));
    uint8_t actor_digest[POISON_AUDIT_DIGEST_BYTES];
    uint8_t correlation_id[POISON_AUDIT_DIGEST_BYTES];
    if(rpc_session_get_audit_context(
           system->rpc_session, request->command_id, actor_digest, correlation_id)) {
        static const char* const actions[] = {
            "update.inspect",
            "update.import",
            "update.stage",
            "update.verify",
            "update.activate",
            "update.health",
            "update.cancel",
            "update.rollback",
            "update.quarantine",
        };
        if((size_t)update_request->operation < COUNT_OF(actions)) {
            PoisonAuditEvent event;
            char metadata[POISON_AUDIT_METADATA_MAX + 1u];
            snprintf(
                metadata,
                sizeof(metadata),
                "id=%.24s;result=%.28s",
                update_request->update_id,
                persisted ? response.content.poison_content_update_status.result : "rejected");
            (void)poison_audit_append(
                poison_audit_get(),
                actor_digest,
                actions[update_request->operation],
                "content-update",
                persisted ? PoisonAuditDecisionAllowed : PoisonAuditDecisionDenied,
                rpc_poison_now_ms(),
                correlation_id,
                metadata,
                &event);
            PoisonDiagnosticCategory diagnostic_category = PoisonDiagnosticCategoryCount;
            const char* diagnostic_summary = NULL;
            if(update_request->operation ==
               PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_STAGE) {
                diagnostic_category = PoisonDiagnosticUpdateStage;
                diagnostic_summary = persisted ? "update staged" : "update stage rejected";
            } else if(
                update_request->operation ==
                PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_HEALTH) {
                diagnostic_category = PoisonDiagnosticUpdateHealth;
                diagnostic_summary = persisted ? "health accepted" : "health rejected";
            } else if(
                update_request->operation ==
                PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_ROLLBACK) {
                diagnostic_category = PoisonDiagnosticUpdateRollback;
                diagnostic_summary = persisted ? "update rolled back" : "rollback rejected";
            }
            if(diagnostic_category != PoisonDiagnosticCategoryCount) {
                (void)poison_diagnostics_record(
                    poison_diagnostics_get(),
                    diagnostic_category,
                    diagnostic_summary,
                    rpc_poison_now_ms(),
                    correlation_id);
            }
            memset(&event, 0, sizeof(event));
        }
        memset(actor_digest, 0, sizeof(actor_digest));
        memset(correlation_id, 0, sizeof(correlation_id));
    }
    if(!persisted) {
        if(update_request->operation ==
               PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_ACTIVATE ||
           (update_request->operation ==
                PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_HEALTH &&
            !update_request->healthy) ||
           update_request->operation ==
               PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_ROLLBACK) {
            update_operation_disarm();
        }
        *system->content_update = *previous;
        memset(previous, 0, sizeof(*previous));
        free(previous);
        rpc_send_and_release_empty(
            system->rpc_session, request->command_id, PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    memset(previous, 0, sizeof(*previous));
    free(previous);
    rpc_send_and_release(system->rpc_session, &response);
    if(update_request->operation ==
       PB_Poison_ContentUpdateOperation_CONTENT_UPDATE_OPERATION_ROLLBACK) {
        furi_timer_pending_callback(rpc_poison_content_update_reset, NULL, 0u);
    }
}

void* rpc_system_poison_alloc(RpcSession* session) {
    furi_assert(session);
    RpcPoisonSystem* system = malloc(sizeof(*system));
    memset(system, 0, sizeof(*system));
    system->rpc_session = session;
    system->content_update = rpc_session_content_update(session);
    system->pairing_store_ready = poison_pairing_registry_init();
    poison_session_init(&system->handshake.session);
    uint64_t total_space = 0u;
    uint64_t free_space = 0u;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(storage_common_fs_info(storage, "/ext", &total_space, &free_space) != FSE_OK)
        free_space = 0u;
    furi_record_close(RECORD_STORAGE);
    rpc_poison_content_update_set_environment(
        system->content_update,
        furi_hal_version_get_hw_target(),
        firmware_api_interface->api_version_major,
        system->content_update->highest_release_sequence,
        free_space);
    rpc_poison_content_update_set_activation_callback(
        system->content_update, rpc_poison_prepare_content_update, system->content_update);
    rpc_poison_content_update_set_rollback_callback(
        system->content_update, rpc_poison_prepare_content_rollback, system->content_update);
    rpc_poison_content_update_set_verification_callback(
        system->content_update, rpc_poison_verify_content_update, system->content_update);
    RpcHandler handler = {
        .message_handler = rpc_poison_pairing_hello_process,
        .decode_submessage = NULL,
        .context = system,
    };
    rpc_add_handler(session, PB_Main_poison_pairing_hello_tag, &handler);
    handler.message_handler = rpc_poison_pairing_confirm_process;
    rpc_add_handler(session, PB_Main_poison_pairing_confirm_tag, &handler);
    handler.message_handler = rpc_poison_session_envelope_process;
    rpc_add_handler(session, PB_Main_poison_session_envelope_tag, &handler);
    handler.message_handler = rpc_poison_content_update_request_process;
    rpc_add_handler(session, PB_Main_poison_content_update_request_tag, &handler);
    handler.message_handler = rpc_poison_diagnostic_snapshot_request_process;
    rpc_add_handler(session, PB_Main_poison_diagnostic_snapshot_request_tag, &handler);
    handler.message_handler = rpc_poison_policy_request_process;
    rpc_add_handler(session, PB_Main_poison_policy_request_tag, &handler);
    handler.message_handler = rpc_poison_resume_request_process;
    rpc_add_handler(session, PB_Main_poison_resume_request_tag, &handler);
    return system;
}

void rpc_system_poison_free(void* context) {
    if(!context) return;
    RpcPoisonSystem* system = context;
    if(system->resume_issued && system->handshake.session.state == PoisonSessionActive) {
        (void)rpc_session_store_resume(
            system->rpc_session, &system->handshake.session, system->resume_token);
    }
    if(system->identity_session_counted) {
        poison_pairing_registry_session_close(system->handshake.session.client_identity_digest);
    }
    poison_session_revoke(&system->handshake.session);
    memset(system, 0, sizeof(*system));
    free(system);
}
