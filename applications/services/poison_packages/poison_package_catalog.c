#include "poison_package_catalog_internal.h"

#include <stdio.h>
#include <string.h>

static bool nonempty_bounded(const char* value, size_t capacity) {
    return value && value[0] != '\0' && strnlen(value, capacity) < capacity;
}

static bool digest_valid(const char* digest) {
    if(!digest || strlen(digest) != POISON_PACKAGE_CATALOG_MAX_DIGEST) return false;
    for(size_t i = 0; i < POISON_PACKAGE_CATALOG_MAX_DIGEST; i++) {
        char c = digest[i];
        if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static int record_compare(
    const PoisonPackageCatalogRecord* left,
    const PoisonPackageCatalogRecord* right) {
    int id_order = strcmp(left->id, right->id);
    if(id_order != 0) return id_order;
    int version_order = strcmp(left->version, right->version);
    if(version_order != 0) return version_order;
    if(left->source < right->source) return -1;
    if(left->source > right->source) return 1;
    return strcmp(left->source_path, right->source_path);
}

void poison_package_catalog_init(PoisonPackageCatalog* catalog) {
    if(catalog) memset(catalog, 0, sizeof(*catalog));
}

bool poison_package_catalog_add(
    PoisonPackageCatalog* catalog,
    const PoisonPackageCatalogRecord* record) {
    if(!catalog || !record || catalog->count >= POISON_PACKAGE_CATALOG_MAX_RECORDS) return false;
    if(!nonempty_bounded(record->id, sizeof(record->id)) ||
       !nonempty_bounded(record->version, sizeof(record->version)) ||
       !nonempty_bounded(record->signer, sizeof(record->signer)) ||
       !nonempty_bounded(record->source_path, sizeof(record->source_path)) ||
       !digest_valid(record->digest)) {
        return false;
    }
    if(record->source > PoisonPackageCatalogSourceLocalRepository ||
       record->freshness > PoisonPackageCatalogFreshnessMissing ||
       record->state > PoisonPackageCatalogRollbackCandidate ||
       (record->signer_revoked != (record->state == PoisonPackageCatalogRevoked)) ||
       (record->signer_revoked && record->verified)) {
        return false;
    }
    for(size_t i = 0; i < catalog->count; i++) {
        const PoisonPackageCatalogRecord* current = &catalog->records[i];
        if(strcmp(current->id, record->id) == 0 &&
           strcmp(current->version, record->version) == 0 && current->source == record->source &&
           strcmp(current->source_path, record->source_path) == 0) {
            return false;
        }
    }

    PoisonPackageCatalogRecord inserted = *record;
    size_t insertion = catalog->count;
    while(insertion > 0 && record_compare(&inserted, &catalog->records[insertion - 1]) < 0) {
        catalog->records[insertion] = catalog->records[insertion - 1];
        insertion--;
    }
    catalog->records[insertion] = inserted;
    catalog->count++;

    for(size_t left = 0u; left < catalog->count; ++left) {
        for(size_t right = left + 1u; right < catalog->count; ++right) {
            PoisonPackageCatalogRecord* first = &catalog->records[left];
            PoisonPackageCatalogRecord* second = &catalog->records[right];
            if(strcmp(first->id, second->id) == 0 &&
               strcmp(first->version, second->version) == 0 &&
               strcmp(first->digest, second->digest) != 0) {
                first->conflicted = true;
                second->conflicted = true;
            }
        }
    }
    return true;
}

const PoisonPackageCatalogRecord* poison_package_catalog_find(
    const PoisonPackageCatalog* catalog,
    const char* id,
    const char* version,
    PoisonPackageCatalogSource source) {
    if(!catalog || !id || !version) return NULL;
    for(size_t i = 0; i < catalog->count; i++) {
        const PoisonPackageCatalogRecord* record = &catalog->records[i];
        if(record->source == source && strcmp(record->id, id) == 0 &&
           strcmp(record->version, version) == 0) {
            return record;
        }
    }
    return NULL;
}

bool poison_package_catalog_is_installable(const PoisonPackageCatalogRecord* record) {
    return record && record->verified && record->compatible && !record->signer_revoked &&
           !record->conflicted && record->freshness == PoisonPackageCatalogFreshnessFresh &&
           record->state == PoisonPackageCatalogAvailable;
}

bool poison_package_catalog_mark_source_missing(
    PoisonPackageCatalog* catalog,
    PoisonPackageCatalogSource source,
    const char* source_path) {
    if(!catalog || !source_path) return false;
    bool changed = false;
    for(size_t i = 0; i < catalog->count; i++) {
        PoisonPackageCatalogRecord* record = &catalog->records[i];
        if(record->source == source && strcmp(record->source_path, source_path) == 0) {
            record->freshness = PoisonPackageCatalogFreshnessMissing;
            if(record->state == PoisonPackageCatalogAvailable)
                record->state = PoisonPackageCatalogQuarantined;
            changed = true;
        }
    }
    return changed;
}

static bool poison_package_catalog_signer_revoked(
    const PoisonPackageAuthorityStore* authorities,
    const char* signer) {
    if(!authorities || !signer) return false;
    for(size_t index = 0u; index < POISON_PACKAGE_AUTHORITY_MAX; ++index) {
        const PoisonPackageAuthority* authority = &authorities->authorities[index];
        if(authority->active && authority->revoked && strcmp(authority->key_id, signer) == 0)
            return true;
    }
    return false;
}

static bool poison_package_catalog_state_from_transaction(
    PoisonPackageState state,
    PoisonPackageCatalogState* catalog_state) {
    if(!catalog_state) return false;
    switch(state) {
    case PoisonPackageInstalled:
    case PoisonPackageActive:
        *catalog_state = PoisonPackageCatalogInstalled;
        return true;
    case PoisonPackageStaged:
    case PoisonPackageVerified:
        *catalog_state = PoisonPackageCatalogStaged;
        return true;
    case PoisonPackageDisabled:
        *catalog_state = PoisonPackageCatalogDisabled;
        return true;
    case PoisonPackageQuarantined:
        *catalog_state = PoisonPackageCatalogQuarantined;
        return true;
    case PoisonPackageRemoved:
        return false;
    }
    return false;
}

static bool poison_package_catalog_copy_text(char* output, size_t output_size, const char* input) {
    if(!output || output_size == 0u || !input) return false;
    const size_t length = strnlen(input, output_size);
    if(length == 0u || length >= output_size) return false;
    memcpy(output, input, length + 1u);
    return true;
}

bool poison_package_catalog_from_manager(
    PoisonPackageCatalog* catalog,
    const PoisonPackageManager* manager,
    const PoisonPackageAuthorityStore* authorities) {
    if(!catalog || !manager) return false;
    poison_package_catalog_init(catalog);
    catalog->generation = manager->generation;

    for(size_t index = 0u; index < POISON_PACKAGE_MAX_RECORDS; ++index) {
        const PoisonPackageRecord* package = &manager->records[index];
        if(!package->occupied || !package->transaction.initialized ||
           package->transaction.state == PoisonPackageRemoved) {
            continue;
        }

        PoisonPackageCatalogRecord current = {0};
        if(!poison_package_catalog_state_from_transaction(
               package->transaction.state, &current.state) ||
           !poison_package_catalog_copy_text(current.id, sizeof(current.id), package->package_id) ||
           !poison_package_catalog_copy_text(
               current.version, sizeof(current.version), package->version) ||
           !poison_package_catalog_copy_text(
               current.signer, sizeof(current.signer), package->signing_key_id) ||
           !poison_package_catalog_copy_text(
               current.digest, sizeof(current.digest), package->digest) ||
           !poison_package_catalog_copy_text(
               current.source_path, sizeof(current.source_path), package->manifest_path)) {
            return false;
        }
        current.source = PoisonPackageCatalogSourceDeviceStorage;
        current.freshness = PoisonPackageCatalogFreshnessFresh;
        current.capability_mask = package->capability_mask;
        current.compatible = true;
        current.verified = package->transaction.content_update.payload_verified ||
                           package->transaction.state == PoisonPackageVerified ||
                           package->transaction.state == PoisonPackageActive ||
                           package->transaction.state == PoisonPackageDisabled;
        current.signer_revoked =
            poison_package_catalog_signer_revoked(authorities, package->signing_key_id);
        if(current.signer_revoked) {
            current.state = PoisonPackageCatalogRevoked;
            current.verified = false;
        }
        if(!poison_package_catalog_add(catalog, &current)) return false;

        if(package->previous_version[0] == '\0' || !digest_valid(package->previous_digest))
            continue;
        PoisonPackageCatalogRecord rollback = current;
        if(!poison_package_catalog_copy_text(
               rollback.version, sizeof(rollback.version), package->previous_version) ||
           !poison_package_catalog_copy_text(
               rollback.digest, sizeof(rollback.digest), package->previous_digest)) {
            return false;
        }
        const int written = snprintf(
            rollback.source_path,
            sizeof(rollback.source_path),
            "/ext/apps/.poison-managed/%s/rollback",
            package->package_id);
        if(written <= 0 || (size_t)written >= sizeof(rollback.source_path)) return false;
        rollback.state = current.signer_revoked ? PoisonPackageCatalogRevoked :
                                                  PoisonPackageCatalogRollbackCandidate;
        rollback.verified = !rollback.signer_revoked;
        rollback.conflicted = false;
        if(!poison_package_catalog_add(catalog, &rollback)) return false;
    }
    return true;
}
