#include <string.h>

#include "../../../../services/rpc/rpc_poison_crypto.h"
#include "../test.h"

static bool poison_crypto_test_decode_hex(const char* hex, uint8_t* output, size_t length) {
    if(!hex || !output || strlen(hex) != length * 2u) return false;
    for(size_t index = 0; index < length; ++index) {
        uint8_t value = 0;
        for(size_t nibble = 0; nibble < 2u; ++nibble) {
            const char digit = hex[index * 2u + nibble];
            uint8_t decoded;
            if(digit >= '0' && digit <= '9') {
                decoded = (uint8_t)(digit - '0');
            } else if(digit >= 'a' && digit <= 'f') {
                decoded = (uint8_t)(digit - 'a' + 10);
            } else {
                return false;
            }
            value = (uint8_t)((value << 4u) | decoded);
        }
        output[index] = value;
    }
    return true;
}

MU_TEST(poison_crypto_sha256_known_vector) {
    const uint8_t expected[POISON_CRYPTO_SHA256_BYTES] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
        0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
        0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    uint8_t digest[POISON_CRYPTO_SHA256_BYTES] = {0};
    const uint8_t input[] = {'a', 'b', 'c'};
    mu_check(poison_crypto_sha256(input, sizeof(input), digest) == PoisonCryptoResultOk);
    mu_check(memcmp(digest, expected, sizeof(expected)) == 0);
}

MU_TEST(poison_crypto_hmac_sha256_known_vector) {
    const uint8_t expected[POISON_CRYPTO_SHA256_BYTES] = {
        0xf7, 0xbc, 0x83, 0xf4, 0x30, 0x53, 0x84, 0x24, 0xb1, 0x32, 0x98,
        0xe6, 0xaa, 0x6f, 0xb1, 0x43, 0xef, 0x4d, 0x59, 0xa1, 0x49, 0x46,
        0x17, 0x59, 0x97, 0x47, 0x9d, 0xbc, 0x2d, 0x1a, 0x3c, 0xd8,
    };
    const uint8_t key[] = {'k', 'e', 'y'};
    const uint8_t input[] = "The quick brown fox jumps over the lazy dog";
    uint8_t digest[POISON_CRYPTO_SHA256_BYTES] = {0};
    mu_check(
        poison_crypto_hmac_sha256(key, sizeof(key), input, sizeof(input) - 1u, digest) ==
        PoisonCryptoResultOk);
    mu_check(memcmp(digest, expected, sizeof(expected)) == 0);
}

MU_TEST(poison_crypto_hkdf_rfc5869_vector) {
    const uint8_t ikm[22] = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    };
    const uint8_t salt[13] = {
        0x00,
        0x01,
        0x02,
        0x03,
        0x04,
        0x05,
        0x06,
        0x07,
        0x08,
        0x09,
        0x0a,
        0x0b,
        0x0c,
    };
    const uint8_t info[10] = {
        0xf0,
        0xf1,
        0xf2,
        0xf3,
        0xf4,
        0xf5,
        0xf6,
        0xf7,
        0xf8,
        0xf9,
    };
    const uint8_t expected[42] = {
        0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a, 0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36,
        0x2f, 0x2a, 0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c, 0x5d, 0xb0, 0x2d, 0x56,
        0xec, 0xc4, 0xc5, 0xbf, 0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18, 0x58, 0x65,
    };
    uint8_t output[sizeof(expected)] = {0};
    mu_check(
        poison_crypto_hkdf_sha256(
            salt, sizeof(salt), ikm, sizeof(ikm), info, sizeof(info), output, sizeof(output)) ==
        PoisonCryptoResultOk);
    mu_check(memcmp(output, expected, sizeof(expected)) == 0);
}

MU_TEST(poison_crypto_p256_shared_secret_matches) {
    uint8_t private_a[POISON_CRYPTO_P256_PRIVATE_BYTES];
    uint8_t public_a[POISON_CRYPTO_P256_PUBLIC_BYTES];
    uint8_t private_b[POISON_CRYPTO_P256_PRIVATE_BYTES];
    uint8_t public_b[POISON_CRYPTO_P256_PUBLIC_BYTES];
    uint8_t shared_a[POISON_CRYPTO_SHARED_SECRET_BYTES];
    uint8_t shared_b[POISON_CRYPTO_SHARED_SECRET_BYTES];
    mu_check(poison_crypto_generate_p256_keypair(private_a, public_a) == PoisonCryptoResultOk);
    mu_check(poison_crypto_generate_p256_keypair(private_b, public_b) == PoisonCryptoResultOk);
    mu_check(
        poison_crypto_p256_shared_secret(private_a, public_b, shared_a) == PoisonCryptoResultOk);
    mu_check(
        poison_crypto_p256_shared_secret(private_b, public_a, shared_b) == PoisonCryptoResultOk);
    mu_check(memcmp(shared_a, shared_b, sizeof(shared_a)) == 0);
}

MU_TEST(poison_crypto_p256_sha256_verifies_upstream_vector) {
    uint8_t public_key[POISON_CRYPTO_P256_PUBLIC_BYTES];
    uint8_t digest[POISON_CRYPTO_SHA256_BYTES];
    uint8_t signature[71];
    mu_check(poison_crypto_test_decode_hex(
        "04e8f573412a810c5f81ecd2d251bb94387e72f28af70dced90ebe75725c97a642"
        "8231069c2b1ef78509a22c59044319f6ed3cb750dfe64c2a282b35967a458ad6",
        public_key,
        sizeof(public_key)));
    mu_check(poison_crypto_test_decode_hex(
        "dee9d4d8b0e40a034602d6e638197998060f6e9f353ae1d10c94cd56476d3c92",
        digest,
        sizeof(digest)));
    mu_check(poison_crypto_test_decode_hex(
        "304502210098a5a1392abe29e4b0a4da3fefe9af0f8c32e5b839ab52ba6a05da9c3b7edd0f"
        "0220596f0e195ae1e58c1e53e9e7f0f030b274348a8c11232101778d89c4943f5ad2",
        signature,
        sizeof(signature)));
    mu_check(
        poison_crypto_verify_p256_sha256(public_key, digest, signature, sizeof(signature)) ==
        PoisonCryptoResultOk);

    signature[sizeof(signature) - 1u] ^= 1u;
    mu_check(
        poison_crypto_verify_p256_sha256(public_key, digest, signature, sizeof(signature)) ==
        PoisonCryptoResultAuthenticationFailure);
}

MU_TEST(poison_crypto_p256_p1363_signature_round_trip_rejects_tampering) {
    uint8_t private_key[POISON_CRYPTO_P256_PRIVATE_BYTES];
    uint8_t public_key[POISON_CRYPTO_P256_PUBLIC_BYTES];
    uint8_t digest[POISON_CRYPTO_SHA256_BYTES];
    uint8_t signature[POISON_CRYPTO_P256_SIGNATURE_MAX];
    size_t signature_length = 0u;
    memset(digest, 0x5a, sizeof(digest));
    mu_check(poison_crypto_generate_p256_keypair(private_key, public_key) == PoisonCryptoResultOk);
    mu_check(
        poison_crypto_sign_p256_sha256(private_key, digest, signature, &signature_length) ==
        PoisonCryptoResultOk);
    mu_check(signature_length == POISON_CRYPTO_P256_PRIVATE_BYTES * 2u);
    mu_check(
        poison_crypto_verify_p256_sha256(public_key, digest, signature, signature_length) ==
        PoisonCryptoResultOk);
    signature[0] ^= 1u;
    mu_check(
        poison_crypto_verify_p256_sha256(public_key, digest, signature, signature_length) ==
        PoisonCryptoResultAuthenticationFailure);
}

MU_TEST_SUITE(poison_crypto_suite) {
    MU_RUN_TEST(poison_crypto_sha256_known_vector);
    MU_RUN_TEST(poison_crypto_hmac_sha256_known_vector);
    MU_RUN_TEST(poison_crypto_hkdf_rfc5869_vector);
    MU_RUN_TEST(poison_crypto_p256_shared_secret_matches);
    MU_RUN_TEST(poison_crypto_p256_sha256_verifies_upstream_vector);
    MU_RUN_TEST(poison_crypto_p256_p1363_signature_round_trip_rejects_tampering);
}

void poison_crypto_run_tests(void) {
    MU_RUN_SUITE(poison_crypto_suite);
}
