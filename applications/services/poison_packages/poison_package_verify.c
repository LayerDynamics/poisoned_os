#include "poison_package_verify.h"

#include <stdio.h>
#include <string.h>

static bool digest(const char* value) {
    if(!value || strlen(value) != 64) return false;
    for(size_t index = 0; index < 64; ++index)
        if(!((value[index] >= '0' && value[index] <= '9') ||
             (value[index] >= 'a' && value[index] <= 'f')))
            return false;
    return true;
}

static bool semantic_version(const char* value, unsigned long parts[3]) {
    int consumed = 0;
    return value &&
           sscanf(value, "%lu.%lu.%lu%n", &parts[0], &parts[1], &parts[2], &consumed) == 3 &&
           value[consumed] == '\0';
}

static int compare_versions(const char* left, const char* right) {
    unsigned long left_parts[3];
    unsigned long right_parts[3];
    if(!semantic_version(left, left_parts) || !semantic_version(right, right_parts)) return -2;
    for(size_t index = 0; index < 3u; ++index) {
        if(left_parts[index] < right_parts[index]) return -1;
        if(left_parts[index] > right_parts[index]) return 1;
    }
    return 0;
}

PoisonPackageVerifyResult poison_package_verify_manifest(
    const char* package_id,
    const char* version,
    const char* entrypoint,
    const char* content_sha256,
    const char* signing_key_id,
    bool signer_revoked,
    const char* installed_version) {
    if(!package_id || !version || !entrypoint || !content_sha256 || !signing_key_id ||
       !package_id[0] || !version[0] || !entrypoint[0] || !signing_key_id[0] ||
       strchr(entrypoint, '/') == entrypoint || strstr(entrypoint, ".."))
        return PoisonPackageVerifyInvalid;
    unsigned long parsed_version[3];
    if(!semantic_version(version, parsed_version)) return PoisonPackageVerifyInvalid;
    if(!digest(content_sha256)) return PoisonPackageVerifyInvalid;
    if(signer_revoked) return PoisonPackageVerifyRevoked;
    if(installed_version) {
        const int comparison = compare_versions(version, installed_version);
        if(comparison == -2) return PoisonPackageVerifyInvalid;
        if(comparison < 0) return PoisonPackageVerifyDowngrade;
    }
    return PoisonPackageVerifyOk;
}
