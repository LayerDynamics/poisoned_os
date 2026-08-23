#include "poison_package_authority.h"

#include <mbedtls/ecp.h>

#include <ctype.h>
#include <string.h>

static bool poison_package_authority_key_id_valid(const char* key_id) {
    if(!key_id || key_id[0] == '\0' ||
       strnlen(key_id, POISON_PACKAGE_KEY_ID_MAX + 1u) > POISON_PACKAGE_KEY_ID_MAX) {
        return false;
    }
    for(const char* cursor = key_id; *cursor; ++cursor) {
        if(!(isalnum((unsigned char)*cursor) || *cursor == '.' || *cursor == '-' ||
             *cursor == '_')) {
            return false;
        }
    }
    return true;
}

static bool poison_package_authority_public_key_valid(
    const uint8_t public_key[POISON_PACKAGE_PUBLIC_KEY_BYTES]) {
    if(!public_key || public_key[0] != 0x04u) return false;
    mbedtls_ecp_group group;
    mbedtls_ecp_point point;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&point);
    int result = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
    if(result == 0) {
        result = mbedtls_ecp_point_read_binary(
            &group, &point, public_key, POISON_PACKAGE_PUBLIC_KEY_BYTES);
    }
    if(result == 0) result = mbedtls_ecp_check_pubkey(&group, &point);
    mbedtls_ecp_point_free(&point);
    mbedtls_ecp_group_free(&group);
    return result == 0;
}

static PoisonPackageAuthority* poison_package_authority_store_find_any(
    PoisonPackageAuthorityStore* store,
    const char* key_id) {
    if(!store || !poison_package_authority_key_id_valid(key_id)) return NULL;
    for(size_t index = 0; index < POISON_PACKAGE_AUTHORITY_MAX; ++index) {
        PoisonPackageAuthority* authority = &store->authorities[index];
        if(authority->active && strcmp(authority->key_id, key_id) == 0) return authority;
    }
    return NULL;
}

void poison_package_authority_store_init(PoisonPackageAuthorityStore* store) {
    if(store) memset(store, 0, sizeof(*store));
}

bool poison_package_authority_store_add(
    PoisonPackageAuthorityStore* store,
    const char* key_id,
    const uint8_t public_key[POISON_PACKAGE_PUBLIC_KEY_BYTES],
    bool revoked) {
    if(!store || !poison_package_authority_key_id_valid(key_id) ||
       !poison_package_authority_public_key_valid(public_key) ||
       poison_package_authority_store_find_any(store, key_id)) {
        return false;
    }
    for(size_t index = 0; index < POISON_PACKAGE_AUTHORITY_MAX; ++index) {
        PoisonPackageAuthority* authority = &store->authorities[index];
        if(!authority->active) {
            memset(authority, 0, sizeof(*authority));
            authority->active = true;
            authority->revoked = revoked;
            strcpy(authority->key_id, key_id);
            memcpy(authority->public_key, public_key, POISON_PACKAGE_PUBLIC_KEY_BYTES);
            ++store->generation;
            return true;
        }
    }
    return false;
}

bool poison_package_authority_store_revoke(PoisonPackageAuthorityStore* store, const char* key_id) {
    PoisonPackageAuthority* authority = poison_package_authority_store_find_any(store, key_id);
    if(!authority || authority->revoked) return false;
    authority->revoked = true;
    ++store->generation;
    return true;
}

const PoisonPackageAuthority* poison_package_authority_store_find(
    const PoisonPackageAuthorityStore* store,
    const char* key_id) {
    PoisonPackageAuthority* authority =
        poison_package_authority_store_find_any((PoisonPackageAuthorityStore*)store, key_id);
    return authority && !authority->revoked ? authority : NULL;
}

size_t poison_package_authority_store_count(const PoisonPackageAuthorityStore* store) {
    if(!store) return 0u;
    size_t count = 0u;
    for(size_t index = 0; index < POISON_PACKAGE_AUTHORITY_MAX; ++index)
        count += store->authorities[index].active && !store->authorities[index].revoked ? 1u : 0u;
    return count;
}

size_t poison_package_authority_store_encode(
    const PoisonPackageAuthorityStore* store,
    uint8_t* output,
    size_t output_capacity) {
    if(!store || !output || output_capacity < 8u) return 0u;
    size_t active_count = 0u;
    size_t required = 8u;
    for(size_t index = 0; index < POISON_PACKAGE_AUTHORITY_MAX; ++index) {
        const PoisonPackageAuthority* authority = &store->authorities[index];
        if(!authority->active) continue;
        ++active_count;
        required += 4u + POISON_PACKAGE_PUBLIC_KEY_BYTES + strlen(authority->key_id);
    }
    if(active_count > UINT8_MAX || required > output_capacity ||
       required > POISON_PACKAGE_AUTHORITY_FILE_MAX) {
        return 0u;
    }
    memcpy(output, "PPK1", 4u);
    output[4] = 1u;
    output[5] = (uint8_t)active_count;
    output[6] = 0u;
    output[7] = 0u;
    size_t cursor = 8u;
    for(size_t index = 0; index < POISON_PACKAGE_AUTHORITY_MAX; ++index) {
        const PoisonPackageAuthority* authority = &store->authorities[index];
        if(!authority->active) continue;
        const size_t key_id_length = strlen(authority->key_id);
        output[cursor++] = (uint8_t)key_id_length;
        output[cursor++] = authority->revoked ? 1u : 0u;
        output[cursor++] = 0u;
        output[cursor++] = 0u;
        memcpy(output + cursor, authority->public_key, POISON_PACKAGE_PUBLIC_KEY_BYTES);
        cursor += POISON_PACKAGE_PUBLIC_KEY_BYTES;
        memcpy(output + cursor, authority->key_id, key_id_length);
        cursor += key_id_length;
    }
    return cursor;
}

bool poison_package_authority_store_decode(
    PoisonPackageAuthorityStore* store,
    const uint8_t* input,
    size_t input_length) {
    if(!store || !input || input_length < 8u || input_length > POISON_PACKAGE_AUTHORITY_FILE_MAX ||
       memcmp(input, "PPK1", 4u) != 0 || input[4] != 1u ||
       input[5] > POISON_PACKAGE_AUTHORITY_MAX || input[6] != 0u || input[7] != 0u) {
        return false;
    }
    PoisonPackageAuthorityStore decoded;
    poison_package_authority_store_init(&decoded);
    size_t cursor = 8u;
    for(size_t index = 0; index < input[5]; ++index) {
        if(input_length - cursor < 4u + POISON_PACKAGE_PUBLIC_KEY_BYTES) return false;
        const size_t key_id_length = input[cursor++];
        const uint8_t flags = input[cursor++];
        const uint8_t reserved_a = input[cursor++];
        const uint8_t reserved_b = input[cursor++];
        if(key_id_length == 0u || key_id_length > POISON_PACKAGE_KEY_ID_MAX || flags > 1u ||
           reserved_a != 0u || reserved_b != 0u ||
           input_length - cursor < POISON_PACKAGE_PUBLIC_KEY_BYTES + key_id_length) {
            return false;
        }
        const uint8_t* public_key = input + cursor;
        cursor += POISON_PACKAGE_PUBLIC_KEY_BYTES;
        char key_id[POISON_PACKAGE_KEY_ID_MAX + 1u];
        memcpy(key_id, input + cursor, key_id_length);
        key_id[key_id_length] = '\0';
        cursor += key_id_length;
        if(!poison_package_authority_store_add(&decoded, key_id, public_key, flags != 0u))
            return false;
    }
    if(cursor != input_length) return false;
    *store = decoded;
    return true;
}
