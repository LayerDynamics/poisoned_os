#pragma once

#include "poison_package_authority.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_PACKAGE_ARCHIVE_MAX_BYTES  (4u * 1024u * 1024u)
#define POISON_PACKAGE_MANIFEST_MAX_BYTES (4096u)
#define POISON_PACKAGE_PAYLOAD_MAX        (32u)
#define POISON_PACKAGE_PAYLOAD_PATH_MAX   (256u)
#define POISON_PACKAGE_FIRMWARE_API_MAX   (64u)

typedef enum {
    PoisonPackageArchiveOk,
    PoisonPackageArchiveInvalid,
    PoisonPackageArchiveIo,
    PoisonPackageArchiveDigestMismatch,
    PoisonPackageArchiveUnknownSigner,
    PoisonPackageArchiveSignatureInvalid,
    PoisonPackageArchiveRevokedSigner,
    PoisonPackageArchiveIncompatible,
    PoisonPackageArchiveDowngrade,
} PoisonPackageArchiveResult;

typedef struct {
    char path[POISON_PACKAGE_PAYLOAD_PATH_MAX + 1u];
    char sha256[65u];
    uint32_t size;
} PoisonPackagePayloadDescriptor;

typedef struct {
    char content_type[33u];
    char package_id[65u];
    char version[33u];
    char firmware_api[POISON_PACKAGE_FIRMWARE_API_MAX + 1u];
    char entrypoint[POISON_PACKAGE_PAYLOAD_PATH_MAX + 1u];
    char content_sha256[65u];
    char signing_key_id[65u];
    char archive_sha256[65u];
    uint64_t capability_mask;
    uint32_t archive_bytes;
    uint32_t release_sequence;
    size_t capability_count;
    char capabilities[32u][65u];
    size_t payload_count;
    PoisonPackagePayloadDescriptor payloads[POISON_PACKAGE_PAYLOAD_MAX];
} PoisonPackageVerifiedArchive;

PoisonPackageArchiveResult poison_package_verify_archive(
    const char* archive_path,
    const char* expected_archive_sha256,
    const PoisonPackageAuthorityStore* authorities,
    uint32_t firmware_api_major,
    uint32_t firmware_api_minor,
    const char* installed_version,
    PoisonPackageVerifiedArchive* verified);

PoisonPackageArchiveResult poison_package_verify_installed_manifest(
    const char* active_root,
    const PoisonPackageAuthorityStore* authorities,
    uint32_t firmware_api_major,
    uint32_t firmware_api_minor,
    PoisonPackageVerifiedArchive* verified);

bool poison_package_extract_verified_archive(
    const char* archive_path,
    const PoisonPackageVerifiedArchive* verified,
    const char* destination_root);

#ifdef __cplusplus
}
#endif
