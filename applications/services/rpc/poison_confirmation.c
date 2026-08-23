#include "poison_confirmation.h"

#include <furi_hal_random.h>

#include <string.h>

static bool poison_confirmation_digest_valid(const uint8_t* digest) {
    if(!digest) return false;
    uint8_t nonzero = 0;
    for(size_t index = 0; index < POISON_CONFIRMATION_DIGEST_BYTES; ++index)
        nonzero |= digest[index];
    return nonzero != 0;
}

static bool poison_confirmation_token_matches(
    const uint8_t expected[POISON_CONFIRMATION_TOKEN_BYTES],
    const uint8_t actual[POISON_CONFIRMATION_TOKEN_BYTES]) {
    if(!actual) return false;
    uint8_t difference = 0;
    for(size_t index = 0; index < POISON_CONFIRMATION_TOKEN_BYTES; ++index) {
        difference |= expected[index] ^ actual[index];
    }
    return difference == 0;
}

PoisonConfirmationResult poison_confirmation_issue(
    PoisonConfirmation* confirmation,
    uint64_t session_id,
    PoisonRole role,
    const uint8_t command_digest[POISON_CONFIRMATION_DIGEST_BYTES],
    const uint8_t target_digest[POISON_CONFIRMATION_DIGEST_BYTES],
    const uint8_t consequence_digest[POISON_CONFIRMATION_DIGEST_BYTES],
    uint32_t policy_version,
    uint64_t now_ms,
    uint32_t ttl_ms,
    bool physical_required) {
    if(!confirmation || session_id == 0 || role >= PoisonRoleCount ||
       !poison_confirmation_digest_valid(command_digest) ||
       !poison_confirmation_digest_valid(target_digest) ||
       !poison_confirmation_digest_valid(consequence_digest) || ttl_ms == 0 ||
       ttl_ms > POISON_CONFIRMATION_MAX_TTL_MS || UINT64_MAX - now_ms < ttl_ms) {
        return PoisonConfirmationResultInvalid;
    }
    memset(confirmation, 0, sizeof(*confirmation));
    confirmation->active = true;
    confirmation->physical_required = physical_required;
    confirmation->session_id = session_id;
    confirmation->role = role;
    memcpy(confirmation->command_digest, command_digest, sizeof(confirmation->command_digest));
    memcpy(confirmation->target_digest, target_digest, sizeof(confirmation->target_digest));
    memcpy(
        confirmation->consequence_digest,
        consequence_digest,
        sizeof(confirmation->consequence_digest));
    furi_hal_random_fill_buf(confirmation->token, sizeof(confirmation->token));
    confirmation->policy_version = policy_version;
    confirmation->expires_at_ms = now_ms + ttl_ms;
    return PoisonConfirmationResultOk;
}

PoisonConfirmationResult poison_confirmation_approve(
    PoisonConfirmation* confirmation,
    uint64_t session_id,
    PoisonRole role,
    const uint8_t command_digest[POISON_CONFIRMATION_DIGEST_BYTES],
    const uint8_t target_digest[POISON_CONFIRMATION_DIGEST_BYTES],
    const uint8_t consequence_digest[POISON_CONFIRMATION_DIGEST_BYTES],
    const uint8_t token[POISON_CONFIRMATION_TOKEN_BYTES],
    uint32_t policy_version,
    uint64_t now_ms,
    bool physical_confirmed) {
    if(!confirmation || !confirmation->active || confirmation->consumed) {
        return PoisonConfirmationResultReplay;
    }
    if(now_ms >= confirmation->expires_at_ms) {
        confirmation->active = false;
        return PoisonConfirmationResultExpired;
    }
    if(confirmation->session_id != session_id || confirmation->role != role ||
       confirmation->policy_version != policy_version || !command_digest || !target_digest ||
       !consequence_digest || !poison_confirmation_token_matches(confirmation->token, token) ||
       memcmp(confirmation->command_digest, command_digest, POISON_CONFIRMATION_DIGEST_BYTES) !=
           0 ||
       memcmp(confirmation->target_digest, target_digest, POISON_CONFIRMATION_DIGEST_BYTES) != 0 ||
       memcmp(
           confirmation->consequence_digest,
           consequence_digest,
           POISON_CONFIRMATION_DIGEST_BYTES) != 0) {
        return PoisonConfirmationResultMismatch;
    }
    if(confirmation->physical_required && !physical_confirmed) {
        return PoisonConfirmationResultMismatch;
    }
    confirmation->consumed = true;
    confirmation->active = false;
    return PoisonConfirmationResultOk;
}

bool poison_confirmation_is_authorized(const PoisonConfirmation* confirmation) {
    return confirmation && confirmation->consumed && !confirmation->active;
}

bool poison_confirmation_caller_requires_confirmation(PoisonConfirmationCaller caller) {
    return caller < PoisonConfirmationCallerCount;
}

const char* poison_confirmation_caller_name(PoisonConfirmationCaller caller) {
    static const char* names[PoisonConfirmationCallerCount] = {
        "storage",
        "evidence",
        "migration",
        "package",
        "update",
        "radio-policy",
        "identity",
        "native-code",
        "application",
        "profile",
        "lesson",
        "recovery",
    };
    return caller < PoisonConfirmationCallerCount ? names[caller] : NULL;
}
