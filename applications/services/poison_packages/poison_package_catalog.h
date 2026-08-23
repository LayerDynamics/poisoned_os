#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_PACKAGE_CATALOG_MAX_RECORDS 32u
#define POISON_PACKAGE_CATALOG_MAX_ID      64u
#define POISON_PACKAGE_CATALOG_MAX_VERSION 32u
#define POISON_PACKAGE_CATALOG_MAX_SIGNER  64u
#define POISON_PACKAGE_CATALOG_MAX_DIGEST  64u
#define POISON_PACKAGE_CATALOG_MAX_SOURCE  256u

typedef enum {
    PoisonPackageCatalogSourceDeviceStorage,
    PoisonPackageCatalogSourceBundledRelease,
    PoisonPackageCatalogSourceImportedFile,
    PoisonPackageCatalogSourceLocalRepository,
} PoisonPackageCatalogSource;

typedef enum {
    PoisonPackageCatalogFreshnessUnknown,
    PoisonPackageCatalogFreshnessFresh,
    PoisonPackageCatalogFreshnessStale,
    PoisonPackageCatalogFreshnessMissing,
} PoisonPackageCatalogFreshness;

typedef enum {
    PoisonPackageCatalogInstalled,
    PoisonPackageCatalogStaged,
    PoisonPackageCatalogAvailable,
    PoisonPackageCatalogIncompatible,
    PoisonPackageCatalogDisabled,
    PoisonPackageCatalogQuarantined,
    PoisonPackageCatalogRevoked,
    PoisonPackageCatalogRollbackCandidate,
} PoisonPackageCatalogState;

typedef struct {
    char id[POISON_PACKAGE_CATALOG_MAX_ID + 1u];
    char version[POISON_PACKAGE_CATALOG_MAX_VERSION + 1u];
    char signer[POISON_PACKAGE_CATALOG_MAX_SIGNER + 1u];
    char digest[POISON_PACKAGE_CATALOG_MAX_DIGEST + 1u];
    char source_path[POISON_PACKAGE_CATALOG_MAX_SOURCE + 1u];
    PoisonPackageCatalogSource source;
    PoisonPackageCatalogFreshness freshness;
    PoisonPackageCatalogState state;
    uint64_t capability_mask;
    bool compatible;
    bool verified;
    bool signer_revoked;
    bool conflicted;
} PoisonPackageCatalogRecord;

typedef struct {
    PoisonPackageCatalogRecord records[POISON_PACKAGE_CATALOG_MAX_RECORDS];
    size_t count;
    uint32_t generation;
} PoisonPackageCatalog;

void poison_package_catalog_init(PoisonPackageCatalog* catalog);
bool poison_package_catalog_add(
    PoisonPackageCatalog* catalog,
    const PoisonPackageCatalogRecord* record);
const PoisonPackageCatalogRecord* poison_package_catalog_find(
    const PoisonPackageCatalog* catalog,
    const char* id,
    const char* version,
    PoisonPackageCatalogSource source);
bool poison_package_catalog_is_installable(const PoisonPackageCatalogRecord* record);
bool poison_package_catalog_mark_source_missing(
    PoisonPackageCatalog* catalog,
    PoisonPackageCatalogSource source,
    const char* source_path);
#ifdef __cplusplus
}
#endif
