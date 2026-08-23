#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PoisonPackageVerifyOk,
    PoisonPackageVerifyInvalid,
    PoisonPackageVerifyRevoked,
    PoisonPackageVerifyDowngrade
} PoisonPackageVerifyResult;

PoisonPackageVerifyResult poison_package_verify_manifest(
    const char* package_id,
    const char* version,
    const char* entrypoint,
    const char* content_sha256,
    const char* signing_key_id,
    bool signer_revoked,
    const char* installed_version);

#ifdef __cplusplus
}
#endif
