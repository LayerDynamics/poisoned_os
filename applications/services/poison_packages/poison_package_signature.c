#include "poison_package_signature.h"

#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/sha256.h>

#include <string.h>

PoisonPackageSignatureResult poison_package_verify_p256_signature(
    const uint8_t public_key[65],
    const uint8_t* canonical_manifest,
    size_t canonical_manifest_length,
    const uint8_t* signature,
    size_t signature_length) {
    if(!public_key || public_key[0] != 0x04u || !canonical_manifest ||
       canonical_manifest_length == 0u || canonical_manifest_length > 4096u || !signature ||
       signature_length == 0u || signature_length > 80u) {
        return PoisonPackageSignatureInvalid;
    }

    uint8_t digest[32u];
    mbedtls_sha256_context sha256;
    mbedtls_sha256_init(&sha256);
    int result = mbedtls_sha256_starts(&sha256, 0);
    if(result == 0)
        result = mbedtls_sha256_update(&sha256, canonical_manifest, canonical_manifest_length);
    if(result == 0) result = mbedtls_sha256_finish(&sha256, digest);
    mbedtls_sha256_free(&sha256);

    mbedtls_ecdsa_context ecdsa;
    mbedtls_ecdsa_init(&ecdsa);
    if(result == 0) {
        result = mbedtls_ecp_group_load(&ecdsa.MBEDTLS_PRIVATE(grp), MBEDTLS_ECP_DP_SECP256R1);
    }
    if(result == 0) {
        result = mbedtls_ecp_point_read_binary(
            &ecdsa.MBEDTLS_PRIVATE(grp), &ecdsa.MBEDTLS_PRIVATE(Q), public_key, 65u);
    }
    if(result == 0) {
        result = mbedtls_ecp_check_pubkey(&ecdsa.MBEDTLS_PRIVATE(grp), &ecdsa.MBEDTLS_PRIVATE(Q));
    }
    if(result == 0) {
        result = mbedtls_ecdsa_read_signature(
            &ecdsa, digest, sizeof(digest), signature, signature_length);
    }
    mbedtls_ecdsa_free(&ecdsa);
    memset(digest, 0, sizeof(digest));
    return result == 0 ? PoisonPackageSignatureOk : PoisonPackageSignatureInvalid;
}
