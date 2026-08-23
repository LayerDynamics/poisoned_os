#include "poison_pairing_store.h"
#include "rpc_poison_crypto.h"

#include <furi.h>
#include <furi_hal_bt.h>
#include <furi_hal_crypto.h>
#include <storage/storage.h>
#include <stdio.h>
#include <string.h>

#define POISON_PAIRING_STORE_VERSION (1u)

typedef struct {
    uint8_t active;
    uint8_t role;
    uint8_t reserved[2u];
    uint8_t key_digest[POISON_PAIRING_KEY_DIGEST_BYTES];
    char client_name[POISON_PAIRING_NAME_MAX + 1u];
    uint32_t generation;
} PoisonPairingDiskRecord;

typedef struct {
    char magic[4u];
    uint32_t version;
    uint32_t generation;
    PoisonPairingDiskRecord records[POISON_PAIRING_MAX_CLIENTS];
    uint8_t digest[POISON_CRYPTO_SHA256_BYTES];
} PoisonPairingDiskState;

static FuriMutex* poison_pairing_registry_mutex;
static PoisonPairingStore poison_pairing_registry_store;
static bool poison_pairing_registry_attempted;
static bool poison_pairing_registry_ready;

static bool poison_pairing_store_authenticate(
    const uint8_t* data,
    size_t data_length,
    uint8_t digest[POISON_CRYPTO_SHA256_BYTES]) {
    static const uint8_t derivation_input[POISON_CRYPTO_KEY_BYTES] = {
        'P', 'o', 'i', 's', 'o', 'n', 'e', 'd', 'O', 'S', '/', 'p', 'a', 'i', 'r', 'i',
        'n', 'g', '/', 's', 't', 'o', 'r', 'e', '/', 'v', '1', 0u,  0u,  0u,  0u,  1u,
    };
    uint8_t iv[16u] = {0};
    uint8_t authentication_key[POISON_CRYPTO_KEY_BYTES] = {0};
    bool loaded = false;
    bool derived = furi_hal_bt_is_alive() && poison_crypto_ensure_device_key();
    if(derived) {
        loaded = furi_hal_crypto_enclave_load_key(FURI_HAL_CRYPTO_ENCLAVE_UNIQUE_KEY_SLOT, iv);
        derived = loaded && furi_hal_crypto_encrypt(
                                derivation_input, authentication_key, sizeof(authentication_key));
    }
    if(loaded)
        derived = furi_hal_crypto_enclave_unload_key(FURI_HAL_CRYPTO_ENCLAVE_UNIQUE_KEY_SLOT) &&
                  derived;
    const bool authenticated =
        derived &&
        poison_crypto_hmac_sha256(
            authentication_key, sizeof(authentication_key), data, data_length, digest) ==
            PoisonCryptoResultOk;
    memset(authentication_key, 0, sizeof(authentication_key));
    return authenticated;
}

void poison_pairing_store_init(PoisonPairingStore* store) {
    if(!store) return;
    memset(store, 0, sizeof(*store));
}

static bool poison_pairing_key_valid(const uint8_t* key_digest) {
    if(!key_digest) return false;
    uint8_t accumulator = 0;
    for(size_t index = 0; index < POISON_PAIRING_KEY_DIGEST_BYTES; ++index) {
        accumulator |= key_digest[index];
    }
    return accumulator != 0;
}

static bool poison_pairing_name_valid(const char* client_name) {
    if(!client_name || client_name[0] == '\0') return false;
    return strnlen(client_name, POISON_PAIRING_NAME_MAX + 1u) <= POISON_PAIRING_NAME_MAX;
}

bool poison_pairing_store_add(
    PoisonPairingStore* store,
    const uint8_t key_digest[POISON_PAIRING_KEY_DIGEST_BYTES],
    const char* client_name,
    PoisonRole role,
    size_t* record_index) {
    if(!store || store->generation == UINT32_MAX || !poison_pairing_key_valid(key_digest) ||
       !poison_pairing_name_valid(client_name) || role >= PoisonRoleCount || !record_index)
        return false;
    if(poison_pairing_store_find(store, key_digest)) return false;
    for(size_t index = 0; index < POISON_PAIRING_MAX_CLIENTS; ++index) {
        PoisonPairingRecord* record = &store->records[index];
        if(!record->active) {
            memset(record, 0, sizeof(*record));
            memcpy(record->key_digest, key_digest, POISON_PAIRING_KEY_DIGEST_BYTES);
            memcpy(record->client_name, client_name, strlen(client_name) + 1u);
            record->role = role;
            record->active = true;
            record->generation = ++store->generation;
            *record_index = index;
            return true;
        }
    }
    return false;
}

bool poison_pairing_store_revoke(PoisonPairingStore* store, size_t record_index) {
    if(!store || store->generation == UINT32_MAX || record_index >= POISON_PAIRING_MAX_CLIENTS)
        return false;
    PoisonPairingRecord* record = &store->records[record_index];
    if(!record->active) return false;
    memset(record, 0, sizeof(*record));
    ++store->generation;
    return true;
}

size_t poison_pairing_store_revoke_all(PoisonPairingStore* store) {
    if(!store) return 0;
    size_t revoked = 0;
    for(size_t index = 0; index < POISON_PAIRING_MAX_CLIENTS; ++index) {
        revoked += poison_pairing_store_revoke(store, index) ? 1u : 0u;
    }
    return revoked;
}

const PoisonPairingRecord* poison_pairing_store_find(
    const PoisonPairingStore* store,
    const uint8_t key_digest[POISON_PAIRING_KEY_DIGEST_BYTES]) {
    if(!store || !poison_pairing_key_valid(key_digest)) return NULL;
    for(size_t index = 0; index < POISON_PAIRING_MAX_CLIENTS; ++index) {
        const PoisonPairingRecord* record = &store->records[index];
        if(record->active &&
           memcmp(record->key_digest, key_digest, POISON_PAIRING_KEY_DIGEST_BYTES) == 0) {
            return record;
        }
    }
    return NULL;
}

size_t poison_pairing_store_count(const PoisonPairingStore* store) {
    if(!store) return 0;
    size_t count = 0;
    for(size_t index = 0; index < POISON_PAIRING_MAX_CLIENTS; ++index) {
        count += store->records[index].active ? 1u : 0u;
    }
    return count;
}

static bool
    poison_pairing_disk_encode(const PoisonPairingStore* store, PoisonPairingDiskState* state) {
    if(!store || !state) return false;
    memset(state, 0, sizeof(*state));
    memcpy(state->magic, "PPRS", sizeof(state->magic));
    state->version = POISON_PAIRING_STORE_VERSION;
    state->generation = store->generation;
    for(size_t index = 0u; index < POISON_PAIRING_MAX_CLIENTS; ++index) {
        const PoisonPairingRecord* source = &store->records[index];
        if(!source->active) continue;
        if(!poison_pairing_key_valid(source->key_digest) ||
           !poison_pairing_name_valid(source->client_name) || source->role >= PoisonRoleCount ||
           source->generation == 0u || source->generation > store->generation) {
            return false;
        }
        PoisonPairingDiskRecord* destination = &state->records[index];
        destination->active = 1u;
        destination->role = (uint8_t)source->role;
        memcpy(destination->key_digest, source->key_digest, sizeof(destination->key_digest));
        memcpy(destination->client_name, source->client_name, strlen(source->client_name) + 1u);
        destination->generation = source->generation;
    }
    return poison_pairing_store_authenticate(
        (const uint8_t*)state, offsetof(PoisonPairingDiskState, digest), state->digest);
}

static bool
    poison_pairing_disk_decode(const PoisonPairingDiskState* state, PoisonPairingStore* store) {
    if(!state || !store || memcmp(state->magic, "PPRS", sizeof(state->magic)) != 0 ||
       state->version != POISON_PAIRING_STORE_VERSION) {
        return false;
    }
    uint8_t digest[POISON_CRYPTO_SHA256_BYTES];
    const bool hashed = poison_pairing_store_authenticate(
        (const uint8_t*)state, offsetof(PoisonPairingDiskState, digest), digest);
    const bool authentic = hashed && memcmp(digest, state->digest, sizeof(digest)) == 0;
    memset(digest, 0, sizeof(digest));
    if(!authentic) return false;

    PoisonPairingStore decoded;
    poison_pairing_store_init(&decoded);
    decoded.generation = state->generation;
    for(size_t index = 0u; index < POISON_PAIRING_MAX_CLIENTS; ++index) {
        const PoisonPairingDiskRecord* source = &state->records[index];
        if(source->active == 0u) continue;
        if(source->active != 1u || source->role >= PoisonRoleCount ||
           !poison_pairing_key_valid(source->key_digest) ||
           !poison_pairing_name_valid(source->client_name) || source->generation == 0u ||
           source->generation > state->generation) {
            return false;
        }
        for(size_t previous = 0u; previous < index; ++previous) {
            if(decoded.records[previous].active && memcmp(
                                                       decoded.records[previous].key_digest,
                                                       source->key_digest,
                                                       sizeof(source->key_digest)) == 0) {
                return false;
            }
        }
        PoisonPairingRecord* destination = &decoded.records[index];
        destination->active = true;
        destination->role = (PoisonRole)source->role;
        memcpy(destination->key_digest, source->key_digest, sizeof(destination->key_digest));
        memcpy(destination->client_name, source->client_name, strlen(source->client_name) + 1u);
        destination->generation = source->generation;
    }
    *store = decoded;
    return true;
}

static bool poison_pairing_store_slot_path(const char* path, uint32_t slot, char slot_path[128u]) {
    return path && slot <= 1u &&
           snprintf(slot_path, 128u, "%s.%lu", path, (unsigned long)slot) < 128;
}

static bool poison_pairing_store_slot_exists(const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool exists = false;
    for(uint32_t slot = 0u; slot < 2u; ++slot) {
        char slot_path[128u];
        if(poison_pairing_store_slot_path(path, slot, slot_path) &&
           storage_common_exists(storage, slot_path)) {
            exists = true;
            break;
        }
    }
    furi_record_close(RECORD_STORAGE);
    return exists;
}

bool poison_pairing_store_save(const PoisonPairingStore* store, const char* path) {
    if(!store || !path || path[0] != '/' || strstr(path, "..")) return false;
    PoisonPairingDiskState state;
    if(!poison_pairing_disk_encode(store, &state)) return false;
    char slot_path[128u];
    if(!poison_pairing_store_slot_path(path, store->generation & 1u, slot_path)) {
        memset(&state, 0, sizeof(state));
        return false;
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = true;
    if(strncmp(path, "/int/.poison/", sizeof("/int/.poison/") - 1u) == 0) {
        ok = storage_simply_mkdir(storage, "/int/.poison");
    }
    File* file = storage_file_alloc(storage);
    if(ok) ok = storage_file_open(file, slot_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) ok = storage_file_write(file, &state, sizeof(state)) == sizeof(state);
    if(ok) ok = storage_file_sync(file);
    if(storage_file_is_open(file)) ok = storage_file_close(file) && ok;
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    memset(&state, 0, sizeof(state));
    return ok;
}

bool poison_pairing_store_load(PoisonPairingStore* store, const char* path) {
    if(!store || !path || path[0] != '/' || strstr(path, "..")) return false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool found = false;
    PoisonPairingStore newest;
    poison_pairing_store_init(&newest);
    for(uint32_t slot = 0u; slot < 2u; ++slot) {
        char slot_path[128u];
        PoisonPairingDiskState state;
        if(!poison_pairing_store_slot_path(path, slot, slot_path)) continue;
        File* file = storage_file_alloc(storage);
        const bool read = storage_file_open(file, slot_path, FSAM_READ, FSOM_OPEN_EXISTING) &&
                          storage_file_size(file) == sizeof(state) &&
                          storage_file_read(file, &state, sizeof(state)) == sizeof(state) &&
                          !storage_file_get_error(file);
        storage_file_free(file);
        PoisonPairingStore candidate;
        poison_pairing_store_init(&candidate);
        if(read && poison_pairing_disk_decode(&state, &candidate) &&
           (!found || candidate.generation > newest.generation)) {
            newest = candidate;
            found = true;
        }
        memset(&state, 0, sizeof(state));
    }
    furi_record_close(RECORD_STORAGE);
    if(found) *store = newest;
    return found;
}

bool poison_pairing_store_recover_corrupt(const char* path) {
    if(!path || path[0] != '/' || strstr(path, "..")) return false;
    PoisonPairingStore existing;
    poison_pairing_store_init(&existing);
    if(!poison_pairing_store_slot_exists(path) || poison_pairing_store_load(&existing, path))
        return false;
    PoisonPairingStore empty;
    poison_pairing_store_init(&empty);
    empty.generation = 1u;
    return poison_pairing_store_save(&empty, path);
}

static bool poison_pairing_registry_lock(void) {
    if(!poison_pairing_registry_mutex) {
        poison_pairing_registry_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    }
    return poison_pairing_registry_mutex &&
           furi_mutex_acquire(poison_pairing_registry_mutex, FuriWaitForever) == FuriStatusOk;
}

void poison_pairing_registry_on_system_start(void) {
    furi_check(!poison_pairing_registry_mutex);
    poison_pairing_registry_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
}

bool poison_pairing_registry_init(void) {
    if(!poison_pairing_registry_lock()) return false;
    if(!poison_pairing_registry_attempted) {
        const bool exists = poison_pairing_store_slot_exists(POISON_PAIRING_STORE_PATH);
        poison_pairing_registry_ready =
            exists ? poison_pairing_store_load(
                         &poison_pairing_registry_store, POISON_PAIRING_STORE_PATH) :
                     true;
        if(!exists) poison_pairing_store_init(&poison_pairing_registry_store);
        poison_pairing_registry_attempted = true;
    }
    const bool ready = poison_pairing_registry_ready;
    furi_check(furi_mutex_release(poison_pairing_registry_mutex) == FuriStatusOk);
    return ready;
}

bool poison_pairing_registry_recover_corrupt(void) {
    if(!poison_pairing_registry_lock()) return false;
    bool recovered = false;
    if(poison_pairing_registry_attempted && !poison_pairing_registry_ready) {
        PoisonPairingStore empty;
        poison_pairing_store_init(&empty);
        recovered = poison_pairing_store_recover_corrupt(POISON_PAIRING_STORE_PATH);
        if(recovered) {
            poison_pairing_registry_store = empty;
            poison_pairing_registry_ready = true;
        }
    }
    furi_check(furi_mutex_release(poison_pairing_registry_mutex) == FuriStatusOk);
    return recovered;
}

bool poison_pairing_registry_add(
    const uint8_t key_digest[POISON_PAIRING_KEY_DIGEST_BYTES],
    const char* client_name,
    PoisonRole role,
    size_t* record_index) {
    if(!poison_pairing_registry_init() || !poison_pairing_registry_lock()) return false;
    PoisonPairingStore next = poison_pairing_registry_store;
    const bool added =
        poison_pairing_store_add(&next, key_digest, client_name, role, record_index);
    const bool saved = added && poison_pairing_store_save(&next, POISON_PAIRING_STORE_PATH);
    if(saved) poison_pairing_registry_store = next;
    furi_check(furi_mutex_release(poison_pairing_registry_mutex) == FuriStatusOk);
    return saved;
}

bool poison_pairing_registry_find(
    const uint8_t key_digest[POISON_PAIRING_KEY_DIGEST_BYTES],
    PoisonPairingRecord* record,
    size_t* record_index) {
    if(!record || !poison_pairing_key_valid(key_digest) || !poison_pairing_registry_init() ||
       !poison_pairing_registry_lock()) {
        return false;
    }
    bool found = false;
    for(size_t index = 0u; index < POISON_PAIRING_MAX_CLIENTS; ++index) {
        const PoisonPairingRecord* candidate = &poison_pairing_registry_store.records[index];
        if(candidate->active &&
           memcmp(candidate->key_digest, key_digest, sizeof(candidate->key_digest)) == 0) {
            *record = *candidate;
            if(record_index) *record_index = index;
            found = true;
            break;
        }
    }
    furi_check(furi_mutex_release(poison_pairing_registry_mutex) == FuriStatusOk);
    return found;
}

size_t poison_pairing_registry_snapshot(PoisonPairingRecord* records, size_t capacity) {
    if(!records || capacity == 0u || !poison_pairing_registry_init() ||
       !poison_pairing_registry_lock()) {
        return 0u;
    }
    const size_t copied = capacity < POISON_PAIRING_MAX_CLIENTS ? capacity :
                                                                  POISON_PAIRING_MAX_CLIENTS;
    memcpy(records, poison_pairing_registry_store.records, copied * sizeof(*records));
    furi_check(furi_mutex_release(poison_pairing_registry_mutex) == FuriStatusOk);
    return copied;
}

bool poison_pairing_registry_revoke(size_t record_index) {
    if(!poison_pairing_registry_init() || !poison_pairing_registry_lock()) return false;
    PoisonPairingStore next = poison_pairing_registry_store;
    const bool revoked = poison_pairing_store_revoke(&next, record_index);
    const bool saved = revoked && poison_pairing_store_save(&next, POISON_PAIRING_STORE_PATH);
    if(saved) poison_pairing_registry_store = next;
    furi_check(furi_mutex_release(poison_pairing_registry_mutex) == FuriStatusOk);
    return saved;
}

size_t poison_pairing_registry_revoke_all(void) {
    if(!poison_pairing_registry_init() || !poison_pairing_registry_lock()) return 0u;
    PoisonPairingStore next = poison_pairing_registry_store;
    const size_t revoked = poison_pairing_store_revoke_all(&next);
    const bool saved = revoked == 0u ||
                       poison_pairing_store_save(&next, POISON_PAIRING_STORE_PATH);
    if(saved) poison_pairing_registry_store = next;
    furi_check(furi_mutex_release(poison_pairing_registry_mutex) == FuriStatusOk);
    return saved ? revoked : 0u;
}

bool poison_pairing_registry_session_open(
    const uint8_t key_digest[POISON_PAIRING_KEY_DIGEST_BYTES]) {
    if(!poison_pairing_key_valid(key_digest) || !poison_pairing_registry_init() ||
       !poison_pairing_registry_lock()) {
        return false;
    }
    bool opened = false;
    for(size_t index = 0u; index < POISON_PAIRING_MAX_CLIENTS; ++index) {
        PoisonPairingRecord* record = &poison_pairing_registry_store.records[index];
        if(record->active &&
           memcmp(record->key_digest, key_digest, sizeof(record->key_digest)) == 0 &&
           record->active_sessions < UINT32_MAX) {
            ++record->active_sessions;
            opened = true;
            break;
        }
    }
    furi_check(furi_mutex_release(poison_pairing_registry_mutex) == FuriStatusOk);
    return opened;
}

void poison_pairing_registry_session_close(
    const uint8_t key_digest[POISON_PAIRING_KEY_DIGEST_BYTES]) {
    if(!poison_pairing_key_valid(key_digest) || !poison_pairing_registry_init() ||
       !poison_pairing_registry_lock()) {
        return;
    }
    for(size_t index = 0u; index < POISON_PAIRING_MAX_CLIENTS; ++index) {
        PoisonPairingRecord* record = &poison_pairing_registry_store.records[index];
        if(record->active &&
           memcmp(record->key_digest, key_digest, sizeof(record->key_digest)) == 0) {
            if(record->active_sessions > 0u) --record->active_sessions;
            break;
        }
    }
    furi_check(furi_mutex_release(poison_pairing_registry_mutex) == FuriStatusOk);
}
