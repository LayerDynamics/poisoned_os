#include "../test.h"
#include "../../../../services/poison_packages/poison_package_catalog.h"
#include "../../../../services/poison_packages/poison_package_catalog_internal.h"
#include "../../../../services/poison_packages/poison_package_manager.h"
#include "../../../../services/rpc/rpc_poison_package_catalog.h"

#include <stdio.h>
#include <string.h>

static PoisonPackageCatalogRecord catalog_record(const char* id, const char* path) {
    PoisonPackageCatalogRecord record = {0};
    snprintf(record.id, sizeof(record.id), "%s", id);
    snprintf(record.version, sizeof(record.version), "1.0.0");
    snprintf(record.signer, sizeof(record.signer), "release");
    snprintf(
        record.digest,
        sizeof(record.digest),
        "0000000000000000000000000000000000000000000000000000000000000000");
    snprintf(record.source_path, sizeof(record.source_path), "%s", path);
    record.source = PoisonPackageCatalogSourceBundledRelease;
    record.freshness = PoisonPackageCatalogFreshnessFresh;
    record.state = PoisonPackageCatalogAvailable;
    record.compatible = true;
    record.verified = true;
    return record;
}

static void catalog_session_pair(PoisonSession* receiver, PoisonSession* sender) {
    uint8_t key[POISON_SESSION_KEY_BYTES];
    memset(key, 0x61, sizeof(key));
    poison_session_init(receiver);
    poison_session_init(sender);
    for(PoisonSession* session = receiver; session;
        session = session == receiver ? sender : NULL) {
        poison_session_begin_negotiation(session, 2u);
        poison_session_begin_confirmation(session, 12u);
        poison_session_set_authentication_key(session, key);
        poison_session_confirm(session, true);
        poison_session_activate(session);
    }
}

MU_TEST(poison_package_catalog_orders_and_rejects_duplicate_sources) {
    PoisonPackageCatalog catalog;
    poison_package_catalog_init(&catalog);
    PoisonPackageCatalogRecord b = catalog_record("org.b", "/ext/catalog/b");
    PoisonPackageCatalogRecord a = catalog_record("org.a", "/ext/catalog/a");
    mu_check(poison_package_catalog_add(&catalog, &b));
    mu_check(poison_package_catalog_add(&catalog, &a));
    mu_check(catalog.count == 2);
    mu_check(strcmp(catalog.records[0].id, "org.a") == 0);
    mu_check(!poison_package_catalog_add(&catalog, &a));
}

MU_TEST(poison_package_catalog_requires_fresh_verified_metadata) {
    PoisonPackageCatalogRecord record = catalog_record("org.a", "/ext/catalog/a");
    mu_check(poison_package_catalog_is_installable(&record));
    record.freshness = PoisonPackageCatalogFreshnessStale;
    mu_check(!poison_package_catalog_is_installable(&record));
    record.freshness = PoisonPackageCatalogFreshnessFresh;
    record.signer_revoked = true;
    mu_check(!poison_package_catalog_is_installable(&record));
}

MU_TEST(poison_package_catalog_quarantines_missing_source) {
    PoisonPackageCatalog catalog;
    poison_package_catalog_init(&catalog);
    PoisonPackageCatalogRecord record = catalog_record("org.a", "/ext/catalog/a");
    mu_check(poison_package_catalog_add(&catalog, &record));
    mu_check(poison_package_catalog_mark_source_missing(
        &catalog, PoisonPackageCatalogSourceBundledRelease, "/ext/catalog/a"));
    mu_check(catalog.records[0].state == PoisonPackageCatalogQuarantined);
    mu_check(!poison_package_catalog_is_installable(&catalog.records[0]));
}

MU_TEST(poison_package_catalog_keeps_revoked_metadata_visible) {
    PoisonPackageCatalog catalog;
    poison_package_catalog_init(&catalog);
    PoisonPackageCatalogRecord record = catalog_record("org.revoked", "/ext/catalog/revoked");
    record.state = PoisonPackageCatalogRevoked;
    record.verified = false;
    record.signer_revoked = true;
    mu_check(poison_package_catalog_add(&catalog, &record));
    mu_check(catalog.count == 1u);
    mu_check(catalog.records[0].signer_revoked);
    mu_check(!poison_package_catalog_is_installable(&catalog.records[0]));
}

MU_TEST(poison_package_catalog_marks_every_conflicting_digest) {
    PoisonPackageCatalog catalog;
    poison_package_catalog_init(&catalog);
    PoisonPackageCatalogRecord bundled = catalog_record("org.conflict", "/ext/catalog/a");
    PoisonPackageCatalogRecord imported = catalog_record("org.conflict", "/ext/import/a");
    imported.source = PoisonPackageCatalogSourceImportedFile;
    memset(imported.digest, '1', 64u);
    imported.digest[64] = '\0';
    mu_check(poison_package_catalog_add(&catalog, &bundled));
    mu_check(poison_package_catalog_add(&catalog, &imported));
    mu_check(catalog.records[0].conflicted);
    mu_check(catalog.records[1].conflicted);
    mu_check(!poison_package_catalog_is_installable(&catalog.records[0]));
    mu_check(!poison_package_catalog_is_installable(&catalog.records[1]));
}

MU_TEST(poison_package_catalog_reconciles_live_manager_and_rollback) {
    PoisonPackageManager manager;
    PoisonPackageAuthorityStore authorities;
    PoisonPackageCatalog catalog;
    poison_package_manager_init(&manager);
    poison_package_authority_store_init(&authorities);

    PoisonPackageRecord* record = &manager.records[0];
    record->occupied = true;
    strcpy(record->package_id, "org.device");
    strcpy(record->version, "2.0.0");
    strcpy(record->previous_version, "1.0.0");
    memset(record->digest, '2', 64u);
    record->digest[64] = '\0';
    memset(record->previous_digest, '1', 64u);
    record->previous_digest[64] = '\0';
    strcpy(record->signing_key_id, "release");
    strcpy(record->manifest_path, "/ext/import/org.device.fapkg");
    record->capability_mask = 0x55u;
    record->transaction.initialized = true;
    record->transaction.state = PoisonPackageActive;
    manager.generation = 7u;

    mu_check(poison_package_catalog_from_manager(&catalog, &manager, &authorities));
    mu_check(catalog.generation == 7u);
    mu_check(catalog.count == 2u);
    const PoisonPackageCatalogRecord* installed = poison_package_catalog_find(
        &catalog, "org.device", "2.0.0", PoisonPackageCatalogSourceDeviceStorage);
    const PoisonPackageCatalogRecord* rollback = poison_package_catalog_find(
        &catalog, "org.device", "1.0.0", PoisonPackageCatalogSourceDeviceStorage);
    mu_check(installed != NULL);
    mu_check(installed->state == PoisonPackageCatalogInstalled);
    mu_check(installed->verified);
    mu_check(installed->compatible);
    mu_check(installed->capability_mask == 0x55u);
    mu_check(rollback != NULL);
    mu_check(rollback->state == PoisonPackageCatalogRollbackCandidate);
    mu_check(rollback->verified);
}

MU_TEST(poison_package_catalog_rpc_lists_authenticated_ordered_records) {
    PoisonPackageCatalog catalog;
    PoisonPackageCatalogRecord output[2] = {0};
    size_t output_count = 0;
    PoisonSession receiver, sender;
    const uint8_t payload[] = {0xA0};
    uint8_t tag[POISON_SESSION_AUTH_TAG_BYTES];
    poison_package_catalog_init(&catalog);
    PoisonPackageCatalogRecord record = catalog_record("org.catalog", "/ext/catalog/a");
    mu_check(poison_package_catalog_add(&catalog, &record));
    catalog_session_pair(&receiver, &sender);
    mu_check(
        poison_session_sign_frame(
            &sender, 2u, 0u, 0u, "package-catalog", payload, sizeof(payload), tag) ==
        PoisonSessionResultOk);
    mu_check(rpc_poison_package_catalog_list_authenticated(
        &receiver,
        2u,
        0u,
        0u,
        "package-catalog",
        payload,
        sizeof(payload),
        tag,
        &catalog,
        output,
        2u,
        &output_count));
    mu_check(output_count == 1u);
    mu_check(strcmp(output[0].id, "org.catalog") == 0);
    mu_check(!rpc_poison_package_catalog_list_authenticated(
        &receiver,
        2u,
        1u,
        0u,
        "wrong-channel",
        payload,
        sizeof(payload),
        tag,
        &catalog,
        output,
        2u,
        &output_count));
}

MU_TEST_SUITE(poison_package_catalog_suite) {
    MU_RUN_TEST(poison_package_catalog_orders_and_rejects_duplicate_sources);
    MU_RUN_TEST(poison_package_catalog_requires_fresh_verified_metadata);
    MU_RUN_TEST(poison_package_catalog_quarantines_missing_source);
    MU_RUN_TEST(poison_package_catalog_keeps_revoked_metadata_visible);
    MU_RUN_TEST(poison_package_catalog_marks_every_conflicting_digest);
    MU_RUN_TEST(poison_package_catalog_reconciles_live_manager_and_rollback);
    MU_RUN_TEST(poison_package_catalog_rpc_lists_authenticated_ordered_records);
}

void poison_package_catalog_run_tests(void) {
    MU_RUN_SUITE(poison_package_catalog_suite);
}
