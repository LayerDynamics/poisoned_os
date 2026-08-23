#pragma once

#include "poison_policy.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POISON_CONFIRMATION_DIGEST_BYTES (32u)
#define POISON_CONFIRMATION_TOKEN_BYTES  (16u)
#define POISON_CONFIRMATION_MAX_TTL_MS   (60000u)

typedef enum {
    PoisonConfirmationResultOk,
    PoisonConfirmationResultInvalid,
    PoisonConfirmationResultExpired,
    PoisonConfirmationResultReplay,
    PoisonConfirmationResultMismatch,
} PoisonConfirmationResult;

typedef enum {
    PoisonConfirmationCallerStorage,
    PoisonConfirmationCallerEvidence,
    PoisonConfirmationCallerMigration,
    PoisonConfirmationCallerPackage,
    PoisonConfirmationCallerUpdate,
    PoisonConfirmationCallerRadioPolicy,
    PoisonConfirmationCallerIdentity,
    PoisonConfirmationCallerNativeCode,
    PoisonConfirmationCallerApplication,
    PoisonConfirmationCallerProfile,
    PoisonConfirmationCallerLesson,
    PoisonConfirmationCallerRecovery,
    PoisonConfirmationCallerCount,
} PoisonConfirmationCaller;

typedef struct {
    bool active;
    bool physical_required;
    bool consumed;
    uint64_t session_id;
    PoisonRole role;
    uint8_t command_digest[POISON_CONFIRMATION_DIGEST_BYTES];
    uint8_t target_digest[POISON_CONFIRMATION_DIGEST_BYTES];
    uint8_t consequence_digest[POISON_CONFIRMATION_DIGEST_BYTES];
    uint8_t token[POISON_CONFIRMATION_TOKEN_BYTES];
    uint32_t policy_version;
    uint64_t expires_at_ms;
} PoisonConfirmation;

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
    bool physical_required);

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
    bool physical_confirmed);

bool poison_confirmation_is_authorized(const PoisonConfirmation* confirmation);
bool poison_confirmation_caller_requires_confirmation(PoisonConfirmationCaller caller);
const char* poison_confirmation_caller_name(PoisonConfirmationCaller caller);
