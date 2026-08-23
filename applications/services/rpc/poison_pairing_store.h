#pragma once

#include "poison_policy.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POISON_PAIRING_MAX_CLIENTS      (8u)
#define POISON_PAIRING_KEY_DIGEST_BYTES (32u)
#define POISON_PAIRING_NAME_MAX         (32u)
#ifdef APP_UNIT_TESTS
#define POISON_PAIRING_STORE_PATH "/ext/.tmp/unit_tests/poison-paired-clients.bin"
#else
#define POISON_PAIRING_STORE_PATH "/int/.poison/paired-clients.bin"
#endif

typedef struct {
    bool active;
    uint8_t key_digest[POISON_PAIRING_KEY_DIGEST_BYTES];
    char client_name[POISON_PAIRING_NAME_MAX + 1u];
    PoisonRole role;
    uint32_t generation;
    uint32_t active_sessions;
} PoisonPairingRecord;

typedef struct {
    PoisonPairingRecord records[POISON_PAIRING_MAX_CLIENTS];
    uint32_t generation;
} PoisonPairingStore;

void poison_pairing_store_init(PoisonPairingStore* store);

bool poison_pairing_store_add(
    PoisonPairingStore* store,
    const uint8_t key_digest[POISON_PAIRING_KEY_DIGEST_BYTES],
    const char* client_name,
    PoisonRole role,
    size_t* record_index);

bool poison_pairing_store_revoke(PoisonPairingStore* store, size_t record_index);

size_t poison_pairing_store_revoke_all(PoisonPairingStore* store);

const PoisonPairingRecord* poison_pairing_store_find(
    const PoisonPairingStore* store,
    const uint8_t key_digest[POISON_PAIRING_KEY_DIGEST_BYTES]);

size_t poison_pairing_store_count(const PoisonPairingStore* store);

bool poison_pairing_store_save(const PoisonPairingStore* store, const char* path);
bool poison_pairing_store_load(PoisonPairingStore* store, const char* path);
bool poison_pairing_store_recover_corrupt(const char* path);

void poison_pairing_registry_on_system_start(void);
bool poison_pairing_registry_init(void);
bool poison_pairing_registry_recover_corrupt(void);
bool poison_pairing_registry_add(
    const uint8_t key_digest[POISON_PAIRING_KEY_DIGEST_BYTES],
    const char* client_name,
    PoisonRole role,
    size_t* record_index);
bool poison_pairing_registry_find(
    const uint8_t key_digest[POISON_PAIRING_KEY_DIGEST_BYTES],
    PoisonPairingRecord* record,
    size_t* record_index);
size_t poison_pairing_registry_snapshot(PoisonPairingRecord* records, size_t capacity);
bool poison_pairing_registry_revoke(size_t record_index);
size_t poison_pairing_registry_revoke_all(void);
bool poison_pairing_registry_session_open(
    const uint8_t key_digest[POISON_PAIRING_KEY_DIGEST_BYTES]);
void poison_pairing_registry_session_close(
    const uint8_t key_digest[POISON_PAIRING_KEY_DIGEST_BYTES]);
