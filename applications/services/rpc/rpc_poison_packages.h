#pragma once

#include "../poison_packages/poison_package_manager.h"
#include "../poison_packages/poison_package_archive.h"
#include "poison_confirmation.h"

#include <poison_packages.pb.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_PACKAGE_CONFIRMATION_TOKEN_BYTES (16u)

typedef struct {
    uint64_t session_id;
    PoisonRole role;
    uint32_t policy_version;
    uint64_t now_ms;
    bool physical_confirmed;
} RpcPoisonPackagesRequestContext;

typedef struct {
    PoisonPackageManager* manager;
    PoisonPackageStorageLayout storage_layout;
    PoisonConfirmation confirmation;
    uint8_t confirmation_token[POISON_PACKAGE_CONFIRMATION_TOKEN_BYTES];
    char confirmation_package_id[POISON_PACKAGE_ID_MAX + 1u];
    char confirmation_digest[POISON_CONTENT_UPDATE_MAX_DIGEST];
    PB_Poison_PackageOperation confirmation_operation;
    uint64_t confirmation_capability_mask;
    bool confirmation_active;
    bool storage_enabled;
    bool persistence_enabled;
    uint32_t hardware_target;
    uint32_t firmware_api_major;
    uint32_t firmware_api_minor;
    uint64_t available_storage_bytes;
} RpcPoisonPackages;

void rpc_poison_packages_init(RpcPoisonPackages* packages, PoisonPackageManager* manager);

void rpc_poison_packages_set_environment(
    RpcPoisonPackages* packages,
    uint32_t hardware_target,
    uint32_t firmware_api_major,
    uint32_t firmware_api_minor,
    uint64_t available_storage_bytes);

void rpc_poison_packages_enable_storage(
    RpcPoisonPackages* packages,
    const PoisonPackageStorageLayout* layout);

bool rpc_poison_packages_process(
    RpcPoisonPackages* packages,
    const PB_Poison_PackageOperationRequest* request,
    const PoisonPackageVerifiedArchive* verified_archive,
    const RpcPoisonPackagesRequestContext* request_context,
    PB_Poison_PackageOperationStatus* status);

#ifdef __cplusplus
}
#endif
