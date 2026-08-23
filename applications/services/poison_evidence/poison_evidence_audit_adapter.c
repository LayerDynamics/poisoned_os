#include "poison_evidence_audit_adapter.h"

#include <mbedtls/sha256.h>
#include <string.h>

void poison_evidence_audit_digest(
    const uint8_t content_sha256[32],
    const uint8_t previous_audit_sha256[32],
    uint8_t output[32]) {
    if(!content_sha256 || !previous_audit_sha256 || !output) return;
    uint8_t input[64];
    memcpy(input, content_sha256, 32);
    memcpy(input + 32, previous_audit_sha256, 32);
    mbedtls_sha256(input, sizeof(input), output, 0);
    memset(input, 0, sizeof(input));
}
