#include "rpc_poison_crypto.h"

#include <furi_hal_crypto.h>
#include <furi_hal_random.h>

#include <mbedtls/ecp.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/md.h>

#include <string.h>

static int poison_crypto_random(void* context, unsigned char* output, size_t length) {
    (void)context;
    if(!output || length > UINT32_MAX) return -1;
    furi_hal_random_fill_buf(output, (uint32_t)length);
    return 0;
}

static bool poison_crypto_valid_buffer(const uint8_t* buffer, size_t length) {
    return length == 0 || buffer != NULL;
}

PoisonCryptoResult poison_crypto_sha256(
    const uint8_t* data,
    size_t data_length,
    uint8_t digest[POISON_CRYPTO_SHA256_BYTES]) {
    if(!poison_crypto_valid_buffer(data, data_length) || !digest) {
        return PoisonCryptoResultInvalid;
    }
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if(!md || mbedtls_md(md, data, data_length, digest) != 0) {
        return PoisonCryptoResultFailure;
    }
    return PoisonCryptoResultOk;
}

PoisonCryptoResult poison_crypto_hmac_sha256(
    const uint8_t* key,
    size_t key_length,
    const uint8_t* data,
    size_t data_length,
    uint8_t digest[POISON_CRYPTO_SHA256_BYTES]) {
    if(!poison_crypto_valid_buffer(key, key_length) ||
       !poison_crypto_valid_buffer(data, data_length) || !digest) {
        return PoisonCryptoResultInvalid;
    }
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if(!md || mbedtls_md_hmac(md, key, key_length, data, data_length, digest) != 0) {
        return PoisonCryptoResultFailure;
    }
    return PoisonCryptoResultOk;
}

PoisonCryptoResult poison_crypto_generate_p256_keypair(
    uint8_t private_key[POISON_CRYPTO_P256_PRIVATE_BYTES],
    uint8_t public_key[POISON_CRYPTO_P256_PUBLIC_BYTES]) {
    if(!private_key || !public_key) return PoisonCryptoResultInvalid;
    mbedtls_ecp_group group;
    mbedtls_mpi private_mpi;
    mbedtls_ecp_point public_point;
    mbedtls_ecp_group_init(&group);
    mbedtls_mpi_init(&private_mpi);
    mbedtls_ecp_point_init(&public_point);
    int result = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
    if(result == 0) {
        result = mbedtls_ecp_gen_keypair(
            &group, &private_mpi, &public_point, poison_crypto_random, NULL);
    }
    size_t public_length = 0;
    if(result == 0) {
        result = mbedtls_ecp_point_write_binary(
            &group,
            &public_point,
            MBEDTLS_ECP_PF_UNCOMPRESSED,
            &public_length,
            public_key,
            POISON_CRYPTO_P256_PUBLIC_BYTES);
    }
    if(result == 0) {
        result =
            mbedtls_mpi_write_binary(&private_mpi, private_key, POISON_CRYPTO_P256_PRIVATE_BYTES);
    }
    mbedtls_ecp_point_free(&public_point);
    mbedtls_mpi_free(&private_mpi);
    mbedtls_ecp_group_free(&group);
    return result == 0 && public_length == POISON_CRYPTO_P256_PUBLIC_BYTES ?
               PoisonCryptoResultOk :
               PoisonCryptoResultFailure;
}

PoisonCryptoResult poison_crypto_p256_shared_secret(
    const uint8_t private_key[POISON_CRYPTO_P256_PRIVATE_BYTES],
    const uint8_t public_key[POISON_CRYPTO_P256_PUBLIC_BYTES],
    uint8_t shared_secret[POISON_CRYPTO_SHARED_SECRET_BYTES]) {
    if(!private_key || !public_key || !shared_secret) return PoisonCryptoResultInvalid;
    mbedtls_ecp_group group;
    mbedtls_mpi private_mpi;
    mbedtls_ecp_point peer_point;
    mbedtls_ecp_point shared_point;
    mbedtls_ecp_group_init(&group);
    mbedtls_mpi_init(&private_mpi);
    mbedtls_ecp_point_init(&peer_point);
    mbedtls_ecp_point_init(&shared_point);
    int result = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
    if(result == 0) result = mbedtls_mpi_read_binary(&private_mpi, private_key, 32);
    if(result == 0) {
        result = mbedtls_ecp_point_read_binary(&group, &peer_point, public_key, 65);
    }
    if(result == 0) {
        result = mbedtls_ecp_mul(
            &group, &shared_point, &private_mpi, &peer_point, poison_crypto_random, NULL);
    }
    if(result == 0) {
        uint8_t encoded_point[POISON_CRYPTO_P256_PUBLIC_BYTES];
        size_t encoded_length = 0;
        result = mbedtls_ecp_point_write_binary(
            &group,
            &shared_point,
            MBEDTLS_ECP_PF_UNCOMPRESSED,
            &encoded_length,
            encoded_point,
            sizeof(encoded_point));
        if(result == 0 && encoded_length == sizeof(encoded_point)) {
            memcpy(shared_secret, &encoded_point[1], POISON_CRYPTO_SHARED_SECRET_BYTES);
        } else {
            result = -1;
        }
        memset(encoded_point, 0, sizeof(encoded_point));
    }
    mbedtls_ecp_point_free(&shared_point);
    mbedtls_ecp_point_free(&peer_point);
    mbedtls_mpi_free(&private_mpi);
    mbedtls_ecp_group_free(&group);
    return result == 0 ? PoisonCryptoResultOk : PoisonCryptoResultFailure;
}

PoisonCryptoResult poison_crypto_verify_p256_sha256(
    const uint8_t public_key[POISON_CRYPTO_P256_PUBLIC_BYTES],
    const uint8_t digest[POISON_CRYPTO_SHA256_BYTES],
    const uint8_t* signature,
    size_t signature_length) {
    if(!public_key || !digest || !signature || signature_length == 0u ||
       signature_length > POISON_CRYPTO_P256_SIGNATURE_MAX || public_key[0] != 0x04u) {
        return PoisonCryptoResultInvalid;
    }

    mbedtls_ecdsa_context context;
    mbedtls_ecdsa_init(&context);
    int result = mbedtls_ecp_group_load(&context.MBEDTLS_PRIVATE(grp), MBEDTLS_ECP_DP_SECP256R1);
    if(result == 0) {
        result = mbedtls_ecp_point_read_binary(
            &context.MBEDTLS_PRIVATE(grp),
            &context.MBEDTLS_PRIVATE(Q),
            public_key,
            POISON_CRYPTO_P256_PUBLIC_BYTES);
    }
    if(result == 0) {
        result =
            mbedtls_ecp_check_pubkey(&context.MBEDTLS_PRIVATE(grp), &context.MBEDTLS_PRIVATE(Q));
    }
    if(result == 0) {
        if(signature_length == POISON_CRYPTO_P256_PRIVATE_BYTES * 2u) {
            mbedtls_mpi r;
            mbedtls_mpi s;
            mbedtls_mpi_init(&r);
            mbedtls_mpi_init(&s);
            result = mbedtls_mpi_read_binary(&r, signature, POISON_CRYPTO_P256_PRIVATE_BYTES);
            if(result == 0) {
                result = mbedtls_mpi_read_binary(
                    &s,
                    signature + POISON_CRYPTO_P256_PRIVATE_BYTES,
                    POISON_CRYPTO_P256_PRIVATE_BYTES);
            }
            if(result == 0) {
                result = mbedtls_ecdsa_verify(
                    &context.MBEDTLS_PRIVATE(grp),
                    digest,
                    POISON_CRYPTO_SHA256_BYTES,
                    &context.MBEDTLS_PRIVATE(Q),
                    &r,
                    &s);
            }
            mbedtls_mpi_free(&s);
            mbedtls_mpi_free(&r);
        } else {
            result = mbedtls_ecdsa_read_signature(
                &context, digest, POISON_CRYPTO_SHA256_BYTES, signature, signature_length);
        }
    }
    mbedtls_ecdsa_free(&context);
    return result == 0 ? PoisonCryptoResultOk : PoisonCryptoResultAuthenticationFailure;
}

PoisonCryptoResult poison_crypto_sign_p256_sha256(
    const uint8_t private_key[POISON_CRYPTO_P256_PRIVATE_BYTES],
    const uint8_t digest[POISON_CRYPTO_SHA256_BYTES],
    uint8_t signature[POISON_CRYPTO_P256_SIGNATURE_MAX],
    size_t* signature_length) {
    if(!private_key || !digest || !signature || !signature_length) {
        return PoisonCryptoResultInvalid;
    }

    mbedtls_ecdsa_context context;
    mbedtls_ecdsa_init(&context);
    int result = mbedtls_ecp_group_load(&context.MBEDTLS_PRIVATE(grp), MBEDTLS_ECP_DP_SECP256R1);
    if(result == 0) {
        result = mbedtls_mpi_read_binary(
            &context.MBEDTLS_PRIVATE(d), private_key, POISON_CRYPTO_P256_PRIVATE_BYTES);
    }
    if(result == 0) {
        result =
            mbedtls_ecp_check_privkey(&context.MBEDTLS_PRIVATE(grp), &context.MBEDTLS_PRIVATE(d));
    }
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    if(result == 0) {
        result = mbedtls_ecdsa_sign(
            &context.MBEDTLS_PRIVATE(grp),
            &r,
            &s,
            &context.MBEDTLS_PRIVATE(d),
            digest,
            POISON_CRYPTO_SHA256_BYTES,
            poison_crypto_random,
            NULL);
    }
    if(result == 0) {
        result = mbedtls_mpi_write_binary(&r, signature, POISON_CRYPTO_P256_PRIVATE_BYTES);
    }
    if(result == 0) {
        result = mbedtls_mpi_write_binary(
            &s, signature + POISON_CRYPTO_P256_PRIVATE_BYTES, POISON_CRYPTO_P256_PRIVATE_BYTES);
    }
    if(result == 0) {
        *signature_length = POISON_CRYPTO_P256_PRIVATE_BYTES * 2u;
    }
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecdsa_free(&context);
    if(result != 0) {
        memset(signature, 0, POISON_CRYPTO_P256_SIGNATURE_MAX);
        *signature_length = 0u;
        return PoisonCryptoResultFailure;
    }
    return PoisonCryptoResultOk;
}

PoisonCryptoResult poison_crypto_hkdf_sha256(
    const uint8_t* salt,
    size_t salt_length,
    const uint8_t* input_key_material,
    size_t input_key_material_length,
    const uint8_t* info,
    size_t info_length,
    uint8_t* output,
    size_t output_length) {
    if(!poison_crypto_valid_buffer(salt, salt_length) ||
       !poison_crypto_valid_buffer(input_key_material, input_key_material_length) ||
       !poison_crypto_valid_buffer(info, info_length) || !output || output_length == 0 ||
       output_length > POISON_CRYPTO_MAX_KDF_BYTES || info_length > 255) {
        return PoisonCryptoResultInvalid;
    }
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if(!md) return PoisonCryptoResultFailure;
    uint8_t zero_salt[32] = {0};
    uint8_t prk[32];
    if(mbedtls_md_hmac(
           md,
           salt_length ? salt : zero_salt,
           32,
           input_key_material,
           input_key_material_length,
           prk) != 0) {
        return PoisonCryptoResultFailure;
    }
    uint8_t previous[32] = {0};
    size_t produced = 0;
    uint8_t counter = 1;
    while(produced < output_length) {
        mbedtls_md_context_t context;
        mbedtls_md_init(&context);
        bool ok = mbedtls_md_setup(&context, md, 1) == 0;
        if(ok) ok = mbedtls_md_hmac_starts(&context, prk, sizeof(prk)) == 0;
        if(ok && counter > 1)
            ok = mbedtls_md_hmac_update(&context, previous, sizeof(previous)) == 0;
        if(ok && info_length) ok = mbedtls_md_hmac_update(&context, info, info_length) == 0;
        if(ok) ok = mbedtls_md_hmac_update(&context, &counter, 1) == 0;
        if(ok) ok = mbedtls_md_hmac_finish(&context, previous) == 0;
        mbedtls_md_free(&context);
        if(!ok) {
            memset(prk, 0, sizeof(prk));
            memset(previous, 0, sizeof(previous));
            return PoisonCryptoResultFailure;
        }
        size_t remaining = output_length - produced;
        size_t copy_length = remaining < sizeof(previous) ? remaining : sizeof(previous);
        memcpy(output + produced, previous, copy_length);
        produced += copy_length;
        if(counter == 255 && produced < output_length) {
            memset(prk, 0, sizeof(prk));
            memset(previous, 0, sizeof(previous));
            return PoisonCryptoResultInvalid;
        }
        ++counter;
    }
    memset(prk, 0, sizeof(prk));
    memset(previous, 0, sizeof(previous));
    return PoisonCryptoResultOk;
}

PoisonCryptoResult poison_crypto_gcm_encrypt(
    const uint8_t key[POISON_CRYPTO_KEY_BYTES],
    const uint8_t iv[POISON_CRYPTO_GCM_IV_BYTES],
    const uint8_t* aad,
    size_t aad_length,
    const uint8_t* plaintext,
    uint8_t* ciphertext,
    size_t length,
    uint8_t tag[POISON_CRYPTO_GCM_TAG_BYTES]) {
    if(!key || !iv || !poison_crypto_valid_buffer(aad, aad_length) ||
       !poison_crypto_valid_buffer(plaintext, length) ||
       !poison_crypto_valid_buffer(ciphertext, length) || !tag)
        return PoisonCryptoResultInvalid;
    FuriHalCryptoGCMState state = furi_hal_crypto_gcm_encrypt_and_tag(
        key, iv, aad, aad_length, plaintext, ciphertext, length, tag);
    return state == FuriHalCryptoGCMStateOk ? PoisonCryptoResultOk : PoisonCryptoResultFailure;
}

PoisonCryptoResult poison_crypto_gcm_decrypt(
    const uint8_t key[POISON_CRYPTO_KEY_BYTES],
    const uint8_t iv[POISON_CRYPTO_GCM_IV_BYTES],
    const uint8_t* aad,
    size_t aad_length,
    const uint8_t* ciphertext,
    uint8_t* plaintext,
    size_t length,
    const uint8_t tag[POISON_CRYPTO_GCM_TAG_BYTES]) {
    if(!key || !iv || !poison_crypto_valid_buffer(aad, aad_length) ||
       !poison_crypto_valid_buffer(ciphertext, length) ||
       !poison_crypto_valid_buffer(plaintext, length) || !tag)
        return PoisonCryptoResultInvalid;
    FuriHalCryptoGCMState state = furi_hal_crypto_gcm_decrypt_and_verify(
        key, iv, aad, aad_length, ciphertext, plaintext, length, tag);
    if(state == FuriHalCryptoGCMStateOk) return PoisonCryptoResultOk;
    if(state == FuriHalCryptoGCMStateAuthFailure) return PoisonCryptoResultAuthenticationFailure;
    return PoisonCryptoResultFailure;
}

bool poison_crypto_ensure_device_key(void) {
    return furi_hal_crypto_enclave_ensure_key(FURI_HAL_CRYPTO_ENCLAVE_UNIQUE_KEY_SLOT);
}
