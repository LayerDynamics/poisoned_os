#pragma once
#include <stdint.h>

void poison_evidence_audit_digest(
    const uint8_t content_sha256[32],
    const uint8_t previous_audit_sha256[32],
    uint8_t output[32]);
