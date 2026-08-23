#include <string.h>

#include "../../../../services/rpc/poison_pairing_store.h"
#include "../../../../services/rpc/poison_policy.h"
#include "../test.h"
#include <storage/storage.h>

static bool poison_pairing_test_tamper_slot(const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool tampered = storage_file_open(file, path, FSAM_READ_WRITE, FSOM_OPEN_EXISTING) &&
                    storage_file_seek(file, 20u, true);
    uint8_t value = 0u;
    if(tampered) tampered = storage_file_read(file, &value, sizeof(value)) == sizeof(value);
    value ^= 0x80u;
    if(tampered) tampered = storage_file_seek(file, 20u, true);
    if(tampered) tampered = storage_file_write(file, &value, sizeof(value)) == sizeof(value);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return tampered;
}

MU_TEST(poison_policy_requires_physical_confirmation_for_sensitive_capabilities) {
    PoisonCapability requested = POISON_CAPABILITY_STATUS | POISON_CAPABILITY_DESTRUCTIVE |
                                 POISON_CAPABILITY_NATIVE | POISON_CAPABILITY_RADIO;
    PoisonPolicyDecision denied =
        poison_policy_evaluate(PoisonRoleOwner, requested, false, false, 3);
    mu_check(!denied.allowed);
    mu_check((denied.granted & POISON_CAPABILITY_STATUS) != 0);
    mu_check((denied.granted & POISON_CAPABILITY_NATIVE) == 0);
    mu_check((denied.granted & POISON_CAPABILITY_DESTRUCTIVE) == 0);

    PoisonPolicyDecision allowed =
        poison_policy_evaluate(PoisonRoleOwner, requested, false, true, 3);
    mu_check(allowed.allowed);
}

MU_TEST(poison_policy_locked_device_is_read_only) {
    PoisonPolicyDecision decision = poison_policy_evaluate(
        PoisonRoleOperator,
        POISON_CAPABILITY_STATUS | POISON_CAPABILITY_CONTROL | POISON_CAPABILITY_FILES,
        true,
        true,
        4);
    mu_check(decision.granted == POISON_CAPABILITY_STATUS);
    mu_check(!decision.allowed);
}

MU_TEST(poison_pairing_store_is_bounded_and_revocable) {
    PoisonPairingStore store;
    poison_pairing_store_init(&store);
    uint8_t digest[POISON_PAIRING_KEY_DIGEST_BYTES] = {0};
    size_t index = 0;
    for(size_t count = 0; count < POISON_PAIRING_MAX_CLIENTS; ++count) {
        memset(digest, (int)(count + 1u), sizeof(digest));
        mu_check(poison_pairing_store_add(&store, digest, "client", PoisonRoleObserver, &index));
    }
    mu_check(poison_pairing_store_count(&store) == POISON_PAIRING_MAX_CLIENTS);
    memset(digest, 0xFF, sizeof(digest));
    mu_check(!poison_pairing_store_add(&store, digest, "overflow", PoisonRoleStudent, &index));
    mu_check(poison_pairing_store_revoke_all(&store) == POISON_PAIRING_MAX_CLIENTS);
    mu_check(poison_pairing_store_count(&store) == 0);
}

MU_TEST(poison_pairing_store_persists_identity_and_role) {
    PoisonPairingStore store;
    poison_pairing_store_init(&store);
    uint8_t digest[POISON_PAIRING_KEY_DIGEST_BYTES];
    memset(digest, 0x42, sizeof(digest));
    size_t index = 0u;
    mu_check(poison_pairing_store_add(&store, digest, "field-console", PoisonRoleOwner, &index));
    const char* path = EXT_PATH(".tmp/poison-paired-clients.bin");
    mu_check(poison_pairing_store_save(&store, path));

    PoisonPairingStore restored;
    poison_pairing_store_init(&restored);
    mu_check(poison_pairing_store_load(&restored, path));
    const PoisonPairingRecord* record = poison_pairing_store_find(&restored, digest);
    mu_check(record != NULL);
    mu_check(strcmp(record->client_name, "field-console") == 0);
    mu_check(record->role == PoisonRoleOwner);
    mu_check(record->active_sessions == 0u);

    uint8_t second_digest[POISON_PAIRING_KEY_DIGEST_BYTES];
    memset(second_digest, 0x24, sizeof(second_digest));
    mu_check(
        poison_pairing_store_add(&store, second_digest, "tablet", PoisonRoleObserver, &index));
    mu_check(poison_pairing_store_save(&store, path));
    char slot_zero[128u];
    char slot_one[128u];
    snprintf(slot_zero, sizeof(slot_zero), "%s.0", path);
    snprintf(slot_one, sizeof(slot_one), "%s.1", path);
    mu_check(poison_pairing_test_tamper_slot(slot_zero));
    poison_pairing_store_init(&restored);
    mu_check(poison_pairing_store_load(&restored, path));
    mu_check(poison_pairing_store_count(&restored) == 1u);

    mu_check(poison_pairing_test_tamper_slot(slot_one));
    poison_pairing_store_init(&restored);
    mu_check(!poison_pairing_store_load(&restored, path));
    mu_check(poison_pairing_store_recover_corrupt(path));
    mu_check(poison_pairing_store_load(&restored, path));
    mu_check(poison_pairing_store_count(&restored) == 0u);
    mu_check(!poison_pairing_store_recover_corrupt(path));
}

MU_TEST_SUITE(poison_policy_suite) {
    MU_RUN_TEST(poison_policy_requires_physical_confirmation_for_sensitive_capabilities);
    MU_RUN_TEST(poison_policy_locked_device_is_read_only);
    MU_RUN_TEST(poison_pairing_store_is_bounded_and_revocable);
    MU_RUN_TEST(poison_pairing_store_persists_identity_and_role);
}

void poison_policy_run_tests(void) {
    MU_RUN_SUITE(poison_policy_suite);
}
