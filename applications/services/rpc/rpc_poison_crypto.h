#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_CRYPTO_P256_PRIVATE_BYTES  (32u)
#define POISON_CRYPTO_P256_PUBLIC_BYTES   (65u)
#define POISON_CRYPTO_SHARED_SECRET_BYTES (32u)
#define POISON_CRYPTO_KEY_BYTES           (32u)
#define POISON_CRYPTO_GCM_IV_BYTES        (12u)
#define POISON_CRYPTO_GCM_TAG_BYTES       (16u)
#define POISON_CRYPTO_SHA256_BYTES        (32u)
#define POISON_CRYPTO_P256_SIGNATURE_MAX  (72u)
#define POISON_CRYPTO_MAX_KDF_BYTES       (64u)

typedef enum {
    PoisonCryptoResultOk,
    PoisonCryptoResultInvalid,
    PoisonCryptoResultFailure,
    PoisonCryptoResultAuthenticationFailure,
} PoisonCryptoResult;

PoisonCryptoResult poison_crypto_generate_p256_keypair(
    uint8_t private_key[POISON_CRYPTO_P256_PRIVATE_BYTES],
    uint8_t public_key[POISON_CRYPTO_P256_PUBLIC_BYTES]);

PoisonCryptoResult poison_crypto_p256_shared_secret(
    const uint8_t private_key[POISON_CRYPTO_P256_PRIVATE_BYTES],
    const uint8_t public_key[POISON_CRYPTO_P256_PUBLIC_BYTES],
    uint8_t shared_secret[POISON_CRYPTO_SHARED_SECRET_BYTES]);

PoisonCryptoResult poison_crypto_verify_p256_sha256(
    const uint8_t public_key[POISON_CRYPTO_P256_PUBLIC_BYTES],
    const uint8_t digest[POISON_CRYPTO_SHA256_BYTES],
    const uint8_t* signature,
    size_t signature_length);

PoisonCryptoResult poison_crypto_sign_p256_sha256(
    const uint8_t private_key[POISON_CRYPTO_P256_PRIVATE_BYTES],
    const uint8_t digest[POISON_CRYPTO_SHA256_BYTES],
    uint8_t signature[POISON_CRYPTO_P256_SIGNATURE_MAX],
    size_t* signature_length);

PoisonCryptoResult poison_crypto_sha256(
    const uint8_t* data,
    size_t data_length,
    uint8_t digest[POISON_CRYPTO_SHA256_BYTES]);

PoisonCryptoResult poison_crypto_hmac_sha256(
    const uint8_t* key,
    size_t key_length,
    const uint8_t* data,
    size_t data_length,
    uint8_t digest[POISON_CRYPTO_SHA256_BYTES]);

PoisonCryptoResult poison_crypto_hkdf_sha256(
    const uint8_t* salt,
    size_t salt_length,
    const uint8_t* input_key_material,
    size_t input_key_material_length,
    const uint8_t* info,
    size_t info_length,
    uint8_t* output,
    size_t output_length);

PoisonCryptoResult poison_crypto_gcm_encrypt(
    const uint8_t key[POISON_CRYPTO_KEY_BYTES],
    const uint8_t iv[POISON_CRYPTO_GCM_IV_BYTES],
    const uint8_t* aad,
    size_t aad_length,
    const uint8_t* plaintext,
    uint8_t* ciphertext,
    size_t length,
    uint8_t tag[POISON_CRYPTO_GCM_TAG_BYTES]);

PoisonCryptoResult poison_crypto_gcm_decrypt(
    const uint8_t key[POISON_CRYPTO_KEY_BYTES],
    const uint8_t iv[POISON_CRYPTO_GCM_IV_BYTES],
    const uint8_t* aad,
    size_t aad_length,
    const uint8_t* ciphertext,
    uint8_t* plaintext,
    size_t length,
    const uint8_t tag[POISON_CRYPTO_GCM_TAG_BYTES]);

bool poison_crypto_ensure_device_key(void);

#ifdef __cplusplus
}
#endif
