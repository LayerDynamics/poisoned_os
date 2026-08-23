#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    PoisonPackageSignatureOk,
    PoisonPackageSignatureInvalid,
} PoisonPackageSignatureResult;

PoisonPackageSignatureResult poison_package_verify_p256_signature(
    const uint8_t public_key[65],
    const uint8_t* canonical_manifest,
    size_t canonical_manifest_length,
    const uint8_t* signature,
    size_t signature_length);
