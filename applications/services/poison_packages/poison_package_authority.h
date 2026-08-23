#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POISON_PACKAGE_AUTHORITY_MAX      (8u)
#define POISON_PACKAGE_KEY_ID_MAX         (64u)
#define POISON_PACKAGE_PUBLIC_KEY_BYTES   (65u)
#define POISON_PACKAGE_AUTHORITY_FILE_MAX (1200u)

typedef struct {
    bool active;
    bool revoked;
    char key_id[POISON_PACKAGE_KEY_ID_MAX + 1u];
    uint8_t public_key[POISON_PACKAGE_PUBLIC_KEY_BYTES];
} PoisonPackageAuthority;

typedef struct {
    PoisonPackageAuthority authorities[POISON_PACKAGE_AUTHORITY_MAX];
    uint32_t generation;
} PoisonPackageAuthorityStore;

void poison_package_authority_store_init(PoisonPackageAuthorityStore* store);
bool poison_package_authority_store_add(
    PoisonPackageAuthorityStore* store,
    const char* key_id,
    const uint8_t public_key[POISON_PACKAGE_PUBLIC_KEY_BYTES],
    bool revoked);
bool poison_package_authority_store_revoke(PoisonPackageAuthorityStore* store, const char* key_id);
const PoisonPackageAuthority* poison_package_authority_store_find(
    const PoisonPackageAuthorityStore* store,
    const char* key_id);
size_t poison_package_authority_store_count(const PoisonPackageAuthorityStore* store);
size_t poison_package_authority_store_encode(
    const PoisonPackageAuthorityStore* store,
    uint8_t* output,
    size_t output_capacity);
bool poison_package_authority_store_decode(
    PoisonPackageAuthorityStore* store,
    const uint8_t* input,
    size_t input_length);
