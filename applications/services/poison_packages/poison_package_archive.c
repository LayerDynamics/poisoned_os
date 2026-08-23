#include "poison_package_archive.h"

#include "poison_package_signature.h"
#include "poison_package_verify.h"

#include <furi.h>
#include <mbedtls/sha256.h>
#include <mjs/common/frozen/frozen.h>
#include <storage/storage.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZIP_LOCAL_HEADER   (0x04034b50u)
#define ZIP_CENTRAL_HEADER (0x02014b50u)
#define ZIP_END_HEADER     (0x06054b50u)
#define ZIP_MEMBER_MAX     (POISON_PACKAGE_PAYLOAD_MAX + 1u)

typedef struct {
    char path[POISON_PACKAGE_PAYLOAD_PATH_MAX + 1u];
    uint32_t local_offset;
    uint32_t data_offset;
    uint32_t size;
    uint32_t crc32;
    bool central_seen;
} PoisonZipMember;

static uint16_t poison_zip_u16(const uint8_t* value) {
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8u);
}

static uint32_t poison_zip_u32(const uint8_t* value) {
    return (uint32_t)value[0] | ((uint32_t)value[1] << 8u) | ((uint32_t)value[2] << 16u) |
           ((uint32_t)value[3] << 24u);
}

static bool poison_file_read_exact(File* file, void* output, size_t length) {
    return length == 0u || storage_file_read(file, output, length) == length;
}

static bool poison_package_hex_digest_valid(const char* value) {
    if(!value || strlen(value) != 64u) return false;
    for(size_t index = 0; index < 64u; ++index) {
        const char c = value[index];
        if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static void poison_package_hex_encode(const uint8_t input[32], char output[65]) {
    static const char hex[] = "0123456789abcdef";
    for(size_t index = 0; index < 32u; ++index) {
        output[index * 2u] = hex[input[index] >> 4u];
        output[index * 2u + 1u] = hex[input[index] & 0x0fu];
    }
    output[64] = '\0';
}

static bool poison_package_copy_bounded(char* output, size_t capacity, const char* input) {
    if(!output || capacity == 0u || !input) return false;
    const size_t length = strlen(input);
    if(length == 0u || length >= capacity) return false;
    memcpy(output, input, length + 1u);
    return true;
}

static bool poison_package_path_valid(const char* path) {
    if(!path || path[0] == '\0' || path[0] == '/' || strchr(path, '\\') ||
       strnlen(path, POISON_PACKAGE_PAYLOAD_PATH_MAX + 1u) > POISON_PACKAGE_PAYLOAD_PATH_MAX) {
        return false;
    }
    const char* segment = path;
    for(const char* cursor = path;; ++cursor) {
        if(*cursor == '/' || *cursor == '\0') {
            const size_t length = (size_t)(cursor - segment);
            if(length == 0u || (length == 1u && segment[0] == '.') ||
               (length == 2u && segment[0] == '.' && segment[1] == '.')) {
                return false;
            }
            if(*cursor == '\0') break;
            segment = cursor + 1;
        }
        if((unsigned char)*cursor < 0x20u) return false;
    }
    return true;
}

static bool poison_package_supported_content_type(const char* value) {
    static const char* const supported[] = {
        "application",
        "firmware",
        "lesson",
        "tool-data",
        "theme",
        "font",
        "icon",
        "font-icon",
        "menu",
        "resource",
        "ui-pack"};
    if(!value) return false;
    for(size_t index = 0; index < COUNT_OF(supported); ++index) {
        if(strcmp(value, supported[index]) == 0) return true;
    }
    return false;
}

static int poison_package_semver_compare(const uint32_t left[3], const uint32_t right[3]) {
    for(size_t index = 0; index < 3u; ++index) {
        if(left[index] < right[index]) return -1;
        if(left[index] > right[index]) return 1;
    }
    return 0;
}

static bool poison_package_semver_parse(const char* value, uint32_t result[3]) {
    if(!value || !result) return false;
    unsigned long major = 0u;
    unsigned long minor = 0u;
    unsigned long patch = 0u;
    int consumed = 0;
    if(sscanf(value, "%lu.%lu.%lu%n", &major, &minor, &patch, &consumed) != 3 ||
       value[consumed] != '\0' || major > UINT32_MAX || minor > UINT32_MAX || patch > UINT32_MAX) {
        return false;
    }
    result[0] = (uint32_t)major;
    result[1] = (uint32_t)minor;
    result[2] = (uint32_t)patch;
    return true;
}

static bool poison_package_firmware_api_compatible(
    const char* requirement,
    uint32_t current_major,
    uint32_t current_minor) {
    if(!requirement || current_major == 0u) return false;
    uint32_t current[3] = {current_major, current_minor, 0u};
    unsigned long minimum[3] = {0u};
    unsigned long maximum[3] = {0u};
    int consumed = 0;
    if(sscanf(
           requirement,
           ">=%lu.%lu.%lu <%lu.%lu.%lu%n",
           &minimum[0],
           &minimum[1],
           &minimum[2],
           &maximum[0],
           &maximum[1],
           &maximum[2],
           &consumed) == 6 &&
       requirement[consumed] == '\0') {
        for(size_t index = 0; index < 3u; ++index) {
            if(minimum[index] > UINT32_MAX || maximum[index] > UINT32_MAX) return false;
        }
        const uint32_t lower[3] = {
            (uint32_t)minimum[0], (uint32_t)minimum[1], (uint32_t)minimum[2]};
        const uint32_t upper[3] = {
            (uint32_t)maximum[0], (uint32_t)maximum[1], (uint32_t)maximum[2]};
        return poison_package_semver_compare(current, lower) >= 0 &&
               poison_package_semver_compare(current, upper) < 0;
    }
    uint32_t exact[3];
    return poison_package_semver_parse(requirement, exact) &&
           poison_package_semver_compare(current, exact) == 0;
}

static bool poison_package_capability_name(const char* capability, uint64_t* capability_mask) {
    if(!capability || !capability_mask || capability[0] == '\0' || strlen(capability) > 64u)
        return false;
    uint64_t bit = 0u;
    if(strcmp(capability, "status") == 0 || strcmp(capability, "device.status.read") == 0 ||
       strncmp(capability, "device.read", 11u) == 0) {
        bit = 1u << 0;
    } else if(
        strcmp(capability, "control") == 0 || strncmp(capability, "ui.", 3u) == 0 ||
        strncmp(capability, "runtime.", 8u) == 0 ||
        strncmp(capability, "notification.", 13u) == 0 ||
        strncmp(capability, "crypto.", 7u) == 0 || strncmp(capability, "compute.", 8u) == 0) {
        bit = 1u << 1;
    } else if(
        strcmp(capability, "launch") == 0 || strcmp(capability, "device.app.run") == 0 ||
        strncmp(capability, "app.", 4u) == 0) {
        bit = 1u << 2;
    } else if(strcmp(capability, "files") == 0 || strncmp(capability, "storage.", 8u) == 0) {
        bit = 1u << 3;
    } else if(strcmp(capability, "evidence") == 0 || strncmp(capability, "evidence.", 9u) == 0) {
        bit = 1u << 4;
    } else if(
        strcmp(capability, "radio") == 0 || strncmp(capability, "radio.", 6u) == 0 ||
        strncmp(capability, "nfc.", 4u) == 0 || strncmp(capability, "lf-rfid.", 8u) == 0 ||
        strncmp(capability, "ibutton.", 8u) == 0 || strncmp(capability, "infrared.", 9u) == 0 ||
        strncmp(capability, "sub-ghz.", 8u) == 0 || strncmp(capability, "gpio.", 5u) == 0 ||
        strncmp(capability, "serial.", 7u) == 0 || strncmp(capability, "ble.", 4u) == 0) {
        bit = 1u << 5;
    } else if(
        strcmp(capability, "native") == 0 || strncmp(capability, "native.", 7u) == 0 ||
        strncmp(capability, "badusb.", 7u) == 0 || strncmp(capability, "usb-hid.", 8u) == 0) {
        bit = 1u << 6;
    } else if(strcmp(capability, "destructive") == 0) {
        bit = 1u << 7;
    } else {
        return false;
    }
    if(strstr(capability, ".write") || strstr(capability, ".transmit") ||
       strstr(capability, ".emulate") || strstr(capability, ".execute") ||
       strstr(capability, ".flash") || strstr(capability, ".delete") ||
       strstr(capability, ".remove")) {
        bit |= 1u << 7;
    }
    *capability_mask |= bit;
    return true;
}

static bool poison_package_canonical_unsigned(
    const uint8_t* manifest,
    size_t manifest_length,
    uint8_t** unsigned_manifest,
    size_t* unsigned_length) {
    if(!manifest || manifest_length < 3u || manifest_length > POISON_PACKAGE_MANIFEST_MAX_BYTES ||
       manifest[0] != '{' || manifest[manifest_length - 2u] != '}' ||
       manifest[manifest_length - 1u] != '\n' || !unsigned_manifest || !unsigned_length) {
        return false;
    }
    const char* text = (const char*)manifest;
    const char marker[] = ",\"signature\":\"";
    const char* signature = strstr(text, marker);
    if(!signature || strstr(signature + sizeof(marker) - 1u, marker)) return false;
    const char* value = signature + sizeof(marker) - 1u;
    const char* close = strchr(value, '"');
    if(!close || close[1] != ',' || close >= text + manifest_length - 2u) return false;
    for(const char* cursor = value; cursor < close; ++cursor) {
        const char c = *cursor;
        if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
             c == '+' || c == '/' || c == '=')) {
            return false;
        }
    }
    const size_t removed = (size_t)((close + 1u) - signature);
    uint8_t* canonical = malloc(manifest_length - removed);
    if(!canonical) return false;
    const size_t prefix = (size_t)(signature - text);
    memcpy(canonical, manifest, prefix);
    memcpy(canonical + prefix, (const uint8_t*)close + 1u, manifest_length - prefix - removed);
    *unsigned_manifest = canonical;
    *unsigned_length = manifest_length - removed;
    return true;
}

static const PoisonPackageAuthority* poison_package_find_authority_any(
    const PoisonPackageAuthorityStore* authorities,
    const char* key_id) {
    if(!authorities || !key_id) return NULL;
    for(size_t index = 0; index < POISON_PACKAGE_AUTHORITY_MAX; ++index) {
        const PoisonPackageAuthority* authority = &authorities->authorities[index];
        if(authority->active && strcmp(authority->key_id, key_id) == 0) return authority;
    }
    return NULL;
}

static bool poison_package_manifest_is_canonical(
    const uint8_t* unsigned_manifest,
    size_t unsigned_length,
    const char* content_type,
    const PoisonPackageVerifiedArchive* verified,
    const char (*capabilities)[65u],
    size_t capability_count) {
    uint8_t* expected = malloc(POISON_PACKAGE_MANIFEST_MAX_BYTES + 1u);
    if(!expected) return false;
    struct json_out output = JSON_OUT_BUF((char*)expected, POISON_PACKAGE_MANIFEST_MAX_BYTES + 1u);
    json_printf(&output, "{\"capabilities\":[");
    for(size_t index = 0; index < capability_count; ++index) {
        if(index > 0u) json_printf(&output, ",");
        json_printf(&output, "%Q", capabilities[index]);
    }
    json_printf(
        &output,
        "],\"contentSha256\":%Q,\"contentType\":%Q,\"entrypoint\":%Q,\"firmwareApi\":%Q,\"id\":%Q,\"packageFormat\":1,\"payloads\":[",
        verified->content_sha256,
        content_type,
        verified->entrypoint,
        verified->firmware_api,
        verified->package_id);
    for(size_t index = 0; index < verified->payload_count; ++index) {
        if(index > 0u) json_printf(&output, ",");
        json_printf(
            &output,
            "{\"path\":%Q,\"sha256\":%Q,\"size\":%lu}",
            verified->payloads[index].path,
            verified->payloads[index].sha256,
            (unsigned long)verified->payloads[index].size);
    }
    json_printf(
        &output,
        "],\"releaseSequence\":%lu,\"signingKeyId\":%Q,\"version\":%Q}\n",
        (unsigned long)verified->release_sequence,
        verified->signing_key_id,
        verified->version);
    const bool canonical = output.u.buf.len == unsigned_length &&
                           output.u.buf.len <= POISON_PACKAGE_MANIFEST_MAX_BYTES &&
                           memcmp(expected, unsigned_manifest, unsigned_length) == 0;
    memset(expected, 0, POISON_PACKAGE_MANIFEST_MAX_BYTES + 1u);
    free(expected);
    return canonical;
}

static PoisonPackageArchiveResult poison_package_parse_manifest(
    const uint8_t* manifest,
    size_t manifest_length,
    const PoisonPackageAuthorityStore* authorities,
    uint32_t firmware_api_major,
    uint32_t firmware_api_minor,
    const char* installed_version,
    PoisonPackageVerifiedArchive* verified) {
    if(!manifest || !authorities || !verified || manifest_length > INT32_MAX) {
        return PoisonPackageArchiveInvalid;
    }
    char* content_type = NULL;
    char* package_id = NULL;
    char* version = NULL;
    char* firmware_api = NULL;
    char* entrypoint = NULL;
    char* content_sha256 = NULL;
    char* signing_key_id = NULL;
    char* signature = NULL;
    size_t capability_count = 0u;
    int signature_length = 0;
    unsigned package_format = 0u;
    const int conversions = json_scanf(
        (const char*)manifest,
        (int)manifest_length,
        "{packageFormat:%u,contentType:%Q,id:%Q,version:%Q,firmwareApi:%Q,entrypoint:%Q,contentSha256:%Q,signingKeyId:%Q,releaseSequence:%u,signature:%V}",
        &package_format,
        &content_type,
        &package_id,
        &version,
        &firmware_api,
        &entrypoint,
        &content_sha256,
        &signing_key_id,
        &verified->release_sequence,
        &signature,
        &signature_length);
    PoisonPackageArchiveResult result = PoisonPackageArchiveInvalid;
    if(conversions != 10 || package_format != 1u || verified->release_sequence == 0u ||
       !poison_package_supported_content_type(content_type) ||
       !poison_package_copy_bounded(
           verified->content_type, sizeof(verified->content_type), content_type) ||
       !poison_package_copy_bounded(
           verified->package_id, sizeof(verified->package_id), package_id) ||
       !poison_package_copy_bounded(verified->version, sizeof(verified->version), version) ||
       !poison_package_copy_bounded(
           verified->firmware_api, sizeof(verified->firmware_api), firmware_api) ||
       !poison_package_copy_bounded(
           verified->entrypoint, sizeof(verified->entrypoint), entrypoint) ||
       !poison_package_copy_bounded(
           verified->content_sha256, sizeof(verified->content_sha256), content_sha256) ||
       !poison_package_copy_bounded(
           verified->signing_key_id, sizeof(verified->signing_key_id), signing_key_id) ||
       !poison_package_path_valid(verified->entrypoint) ||
       !poison_package_hex_digest_valid(verified->content_sha256) || signature_length <= 0 ||
       signature_length > 80) {
        goto cleanup;
    }
    if(poison_package_verify_manifest(
           verified->package_id,
           verified->version,
           verified->entrypoint,
           verified->content_sha256,
           verified->signing_key_id,
           false,
           installed_version) == PoisonPackageVerifyDowngrade) {
        result = PoisonPackageArchiveDowngrade;
        goto cleanup;
    }
    if(!poison_package_firmware_api_compatible(
           verified->firmware_api, firmware_api_major, firmware_api_minor)) {
        result = PoisonPackageArchiveIncompatible;
        goto cleanup;
    }

    for(size_t index = 0; index < POISON_PACKAGE_PAYLOAD_MAX; ++index) {
        struct json_token token = JSON_INVALID_TOKEN;
        if(json_scanf_array_elem(
               (const char*)manifest, (int)manifest_length, ".payloads", (int)index, &token) < 0) {
            break;
        }
        char* path = NULL;
        char* sha256 = NULL;
        unsigned long size = 0u;
        if(token.type != JSON_TYPE_OBJECT_END ||
           json_scanf(
               token.ptr, token.len, "{path:%Q,sha256:%Q,size:%lu}", &path, &sha256, &size) != 3 ||
           size > UINT32_MAX || !poison_package_path_valid(path) ||
           !poison_package_hex_digest_valid(sha256) ||
           !poison_package_copy_bounded(
               verified->payloads[index].path, sizeof(verified->payloads[index].path), path) ||
           !poison_package_copy_bounded(
               verified->payloads[index].sha256,
               sizeof(verified->payloads[index].sha256),
               sha256)) {
            free(path);
            free(sha256);
            goto cleanup;
        }
        verified->payloads[index].size = (uint32_t)size;
        free(path);
        free(sha256);
        for(size_t prior = 0; prior < index; ++prior) {
            if(strcmp(verified->payloads[prior].path, verified->payloads[index].path) == 0)
                goto cleanup;
        }
        ++verified->payload_count;
    }
    if(verified->payload_count == 0u) goto cleanup;
    struct json_token excess = JSON_INVALID_TOKEN;
    if(json_scanf_array_elem(
           (const char*)manifest,
           (int)manifest_length,
           ".payloads",
           POISON_PACKAGE_PAYLOAD_MAX,
           &excess) >= 0) {
        goto cleanup;
    }
    bool entrypoint_present = false;
    for(size_t index = 0; index < verified->payload_count; ++index) {
        entrypoint_present |= strcmp(verified->entrypoint, verified->payloads[index].path) == 0;
    }
    if(!entrypoint_present) goto cleanup;

    for(size_t index = 0; index < 32u; ++index) {
        struct json_token token = JSON_INVALID_TOKEN;
        if(json_scanf_array_elem(
               (const char*)manifest, (int)manifest_length, ".capabilities", (int)index, &token) <
           0) {
            break;
        }
        if(token.type != JSON_TYPE_STRING || token.len <= 0 || token.len > 64) goto cleanup;
        char* capability = verified->capabilities[capability_count];
        const int decoded = json_unescape(token.ptr, token.len, capability, 64u);
        if(decoded <= 0 || decoded > 64) goto cleanup;
        capability[decoded] = '\0';
        if(!poison_package_capability_name(capability, &verified->capability_mask)) goto cleanup;
        for(size_t prior = 0; prior < capability_count; ++prior) {
            if(strcmp(verified->capabilities[prior], capability) == 0) goto cleanup;
        }
        ++capability_count;
    }
    verified->capability_count = capability_count;

    const PoisonPackageAuthority* authority =
        poison_package_find_authority_any(authorities, verified->signing_key_id);
    if(!authority) {
        result = PoisonPackageArchiveUnknownSigner;
        goto cleanup;
    }
    if(authority->revoked) {
        result = PoisonPackageArchiveRevokedSigner;
        goto cleanup;
    }
    uint8_t* unsigned_manifest = NULL;
    size_t unsigned_length = 0u;
    if(!poison_package_canonical_unsigned(
           manifest, manifest_length, &unsigned_manifest, &unsigned_length)) {
        goto cleanup;
    }
    if(!poison_package_manifest_is_canonical(
           unsigned_manifest,
           unsigned_length,
           content_type,
           verified,
           verified->capabilities,
           capability_count)) {
        memset(unsigned_manifest, 0, unsigned_length);
        free(unsigned_manifest);
        goto cleanup;
    }
    result = poison_package_verify_p256_signature(
                 authority->public_key,
                 unsigned_manifest,
                 unsigned_length,
                 (const uint8_t*)signature,
                 (size_t)signature_length) == PoisonPackageSignatureOk ?
                 PoisonPackageArchiveOk :
                 PoisonPackageArchiveSignatureInvalid;
    memset(unsigned_manifest, 0, unsigned_length);
    free(unsigned_manifest);

cleanup:
    free(content_type);
    free(package_id);
    free(version);
    free(firmware_api);
    free(entrypoint);
    free(content_sha256);
    free(signing_key_id);
    if(signature) {
        memset(signature, 0, (size_t)(signature_length > 0 ? signature_length : 0));
        free(signature);
    }
    return result;
}

PoisonPackageArchiveResult poison_package_verify_installed_manifest(
    const char* active_root,
    const PoisonPackageAuthorityStore* authorities,
    uint32_t firmware_api_major,
    uint32_t firmware_api_minor,
    PoisonPackageVerifiedArchive* verified) {
    if(!active_root || active_root[0] != '/' || !authorities || !verified ||
       strnlen(active_root, POISON_PACKAGE_PAYLOAD_PATH_MAX + 1u) >
           POISON_PACKAGE_PAYLOAD_PATH_MAX ||
       strstr(active_root, "..") || strchr(active_root, '\\')) {
        return PoisonPackageArchiveInvalid;
    }
    char manifest_path[POISON_PACKAGE_PAYLOAD_PATH_MAX + 1u];
    const int written =
        snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", active_root);
    if(written <= 0 || written >= (int)sizeof(manifest_path)) {
        return PoisonPackageArchiveInvalid;
    }
    memset(verified, 0, sizeof(*verified));
    uint8_t* manifest = malloc(POISON_PACKAGE_MANIFEST_MAX_BYTES + 1u);
    if(!manifest) return PoisonPackageArchiveIo;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    FileInfo info;
    const bool loaded = storage_common_stat(storage, manifest_path, &info) == FSE_OK &&
                        (info.flags & FSF_DIRECTORY) == 0u && info.size > 0u &&
                        info.size <= POISON_PACKAGE_MANIFEST_MAX_BYTES &&
                        storage_file_open(file, manifest_path, FSAM_READ, FSOM_OPEN_EXISTING) &&
                        storage_file_read(file, manifest, info.size) == info.size &&
                        !storage_file_get_error(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    PoisonPackageArchiveResult result = PoisonPackageArchiveIo;
    if(loaded) {
        manifest[info.size] = '\0';
        result = poison_package_parse_manifest(
            manifest,
            info.size,
            authorities,
            firmware_api_major,
            firmware_api_minor,
            NULL,
            verified);
    }
    memset(manifest, 0, POISON_PACKAGE_MANIFEST_MAX_BYTES + 1u);
    free(manifest);
    return result;
}

static bool poison_package_hash_file(File* file, uint32_t size, uint8_t digest[32]) {
    uint8_t buffer[256u];
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool ok = mbedtls_sha256_starts(&hash, 0) == 0;
    uint32_t remaining = size;
    while(ok && remaining > 0u) {
        const size_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        ok = poison_file_read_exact(file, buffer, chunk) &&
             mbedtls_sha256_update(&hash, buffer, chunk) == 0;
        remaining -= ok ? (uint32_t)chunk : 0u;
    }
    ok = ok && mbedtls_sha256_finish(&hash, digest) == 0;
    mbedtls_sha256_free(&hash);
    memset(buffer, 0, sizeof(buffer));
    return ok;
}

static PoisonZipMember*
    poison_package_find_member(PoisonZipMember* members, size_t member_count, const char* path) {
    for(size_t index = 0; index < member_count; ++index) {
        if(strcmp(members[index].path, path) == 0) return &members[index];
    }
    return NULL;
}

static bool poison_package_verify_members(
    File* file,
    PoisonZipMember* members,
    size_t member_count,
    PoisonPackageVerifiedArchive* verified) {
    if(member_count != verified->payload_count + 1u ||
       strcmp(members[0].path, "manifest.json") != 0) {
        return false;
    }
    size_t order[POISON_PACKAGE_PAYLOAD_MAX];
    for(size_t index = 0; index < verified->payload_count; ++index)
        order[index] = index;
    for(size_t left = 0; left < verified->payload_count; ++left) {
        for(size_t right = left + 1u; right < verified->payload_count; ++right) {
            if(strcmp(verified->payloads[order[left]].path, verified->payloads[order[right]].path) >
               0) {
                const size_t swap = order[left];
                order[left] = order[right];
                order[right] = swap;
            }
        }
    }

    mbedtls_sha256_context content_hash;
    mbedtls_sha256_init(&content_hash);
    bool ok = mbedtls_sha256_starts(&content_hash, 0) == 0;
    uint8_t buffer[256u];
    for(size_t sorted = 0; ok && sorted < verified->payload_count; ++sorted) {
        PoisonPackagePayloadDescriptor* payload = &verified->payloads[order[sorted]];
        PoisonZipMember* member = poison_package_find_member(members, member_count, payload->path);
        if(!member || member->size != payload->size ||
           !storage_file_seek(file, member->data_offset, true)) {
            ok = false;
            break;
        }
        ok = mbedtls_sha256_update(
                 &content_hash, (const uint8_t*)payload->path, strlen(payload->path)) == 0;
        const uint8_t separator = 0u;
        ok = ok && mbedtls_sha256_update(&content_hash, &separator, 1u) == 0;
        mbedtls_sha256_context payload_hash;
        mbedtls_sha256_init(&payload_hash);
        ok = ok && mbedtls_sha256_starts(&payload_hash, 0) == 0;
        uint32_t remaining = member->size;
        while(ok && remaining > 0u) {
            const size_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
            ok = poison_file_read_exact(file, buffer, chunk) &&
                 mbedtls_sha256_update(&payload_hash, buffer, chunk) == 0 &&
                 mbedtls_sha256_update(&content_hash, buffer, chunk) == 0;
            remaining -= ok ? (uint32_t)chunk : 0u;
        }
        uint8_t payload_digest[32u];
        ok = ok && mbedtls_sha256_finish(&payload_hash, payload_digest) == 0;
        mbedtls_sha256_free(&payload_hash);
        char payload_hex[65u];
        poison_package_hex_encode(payload_digest, payload_hex);
        ok = ok && strcmp(payload_hex, payload->sha256) == 0;
        memset(payload_digest, 0, sizeof(payload_digest));
        memset(payload_hex, 0, sizeof(payload_hex));
    }
    uint8_t content_digest[32u];
    ok = ok && mbedtls_sha256_finish(&content_hash, content_digest) == 0;
    mbedtls_sha256_free(&content_hash);
    char content_hex[65u];
    poison_package_hex_encode(content_digest, content_hex);
    ok = ok && strcmp(content_hex, verified->content_sha256) == 0;
    memset(buffer, 0, sizeof(buffer));
    memset(content_digest, 0, sizeof(content_digest));
    memset(content_hex, 0, sizeof(content_hex));
    return ok;
}

PoisonPackageArchiveResult poison_package_verify_archive(
    const char* archive_path,
    const char* expected_archive_sha256,
    const PoisonPackageAuthorityStore* authorities,
    uint32_t firmware_api_major,
    uint32_t firmware_api_minor,
    const char* installed_version,
    PoisonPackageVerifiedArchive* verified) {
    if(!archive_path || !poison_package_hex_digest_valid(expected_archive_sha256) ||
       !authorities || !verified) {
        return PoisonPackageArchiveInvalid;
    }
    memset(verified, 0, sizeof(*verified));
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    PoisonPackageArchiveResult result = PoisonPackageArchiveIo;
    if(!storage_file_open(file, archive_path, FSAM_READ, FSOM_OPEN_EXISTING)) goto cleanup;
    const uint64_t archive_size = storage_file_size(file);
    if(archive_size == 0u || archive_size > POISON_PACKAGE_ARCHIVE_MAX_BYTES ||
       archive_size > UINT32_MAX || !storage_file_seek(file, 0u, true)) {
        result = PoisonPackageArchiveInvalid;
        goto cleanup;
    }
    uint8_t archive_digest[32u];
    if(!poison_package_hash_file(file, (uint32_t)archive_size, archive_digest)) goto cleanup;
    poison_package_hex_encode(archive_digest, verified->archive_sha256);
    memset(archive_digest, 0, sizeof(archive_digest));
    if(strcmp(verified->archive_sha256, expected_archive_sha256) != 0) {
        result = PoisonPackageArchiveDigestMismatch;
        goto cleanup;
    }
    verified->archive_bytes = (uint32_t)archive_size;

    PoisonZipMember* members = calloc(ZIP_MEMBER_MAX, sizeof(*members));
    uint8_t* manifest = malloc(POISON_PACKAGE_MANIFEST_MAX_BYTES + 1u);
    if(!members || !manifest) {
        free(members);
        free(manifest);
        goto cleanup;
    }
    size_t member_count = 0u;
    size_t manifest_length = 0u;
    uint32_t central_offset = 0u;
    if(!storage_file_seek(file, 0u, true)) goto archive_cleanup;
    while(storage_file_tell(file) + 4u <= archive_size) {
        uint8_t signature_bytes[4u];
        const uint32_t local_offset = (uint32_t)storage_file_tell(file);
        if(!poison_file_read_exact(file, signature_bytes, sizeof(signature_bytes)))
            goto archive_cleanup;
        const uint32_t signature = poison_zip_u32(signature_bytes);
        if(signature == ZIP_CENTRAL_HEADER) {
            central_offset = local_offset;
            if(!storage_file_seek(file, local_offset, true)) goto archive_cleanup;
            break;
        }
        if(signature != ZIP_LOCAL_HEADER || member_count >= ZIP_MEMBER_MAX) {
            result = PoisonPackageArchiveInvalid;
            goto archive_cleanup;
        }
        uint8_t header[26u];
        if(!poison_file_read_exact(file, header, sizeof(header))) goto archive_cleanup;
        const uint16_t flags = poison_zip_u16(header + 2u);
        const uint16_t compression = poison_zip_u16(header + 4u);
        const uint32_t crc32 = poison_zip_u32(header + 10u);
        const uint32_t compressed = poison_zip_u32(header + 14u);
        const uint32_t uncompressed = poison_zip_u32(header + 18u);
        const uint16_t name_length = poison_zip_u16(header + 22u);
        const uint16_t extra_length = poison_zip_u16(header + 24u);
        if(flags != 0u || compression != 0u || compressed != uncompressed ||
           compressed == UINT32_MAX || name_length == 0u ||
           name_length > POISON_PACKAGE_PAYLOAD_PATH_MAX || extra_length > 1024u ||
           storage_file_tell(file) + name_length + extra_length + compressed > archive_size) {
            result = PoisonPackageArchiveInvalid;
            goto archive_cleanup;
        }
        PoisonZipMember* member = &members[member_count];
        if(!poison_file_read_exact(file, member->path, name_length)) goto archive_cleanup;
        member->path[name_length] = '\0';
        if(!poison_package_path_valid(member->path) ||
           poison_package_find_member(members, member_count, member->path) ||
           !storage_file_seek(file, extra_length, false)) {
            result = PoisonPackageArchiveInvalid;
            goto archive_cleanup;
        }
        member->local_offset = local_offset;
        member->data_offset = (uint32_t)storage_file_tell(file);
        member->size = uncompressed;
        member->crc32 = crc32;
        if(member_count == 0u) {
            if(strcmp(member->path, "manifest.json") != 0 || member->size == 0u ||
               member->size > POISON_PACKAGE_MANIFEST_MAX_BYTES ||
               !poison_file_read_exact(file, manifest, member->size)) {
                result = PoisonPackageArchiveInvalid;
                goto archive_cleanup;
            }
            manifest_length = member->size;
            manifest[manifest_length] = '\0';
        } else if(!storage_file_seek(file, member->size, false)) {
            goto archive_cleanup;
        }
        ++member_count;
    }
    if(central_offset == 0u || member_count == 0u || manifest_length == 0u) {
        result = PoisonPackageArchiveInvalid;
        goto archive_cleanup;
    }

    size_t central_count = 0u;
    while(storage_file_tell(file) + 4u <= archive_size) {
        uint8_t signature_bytes[4u];
        if(!poison_file_read_exact(file, signature_bytes, sizeof(signature_bytes)))
            goto archive_cleanup;
        const uint32_t signature = poison_zip_u32(signature_bytes);
        if(signature == ZIP_END_HEADER) {
            uint8_t end[18u];
            if(!poison_file_read_exact(file, end, sizeof(end)) || poison_zip_u16(end) != 0u ||
               poison_zip_u16(end + 2u) != 0u || poison_zip_u16(end + 4u) != member_count ||
               poison_zip_u16(end + 6u) != member_count ||
               poison_zip_u32(end + 12u) != central_offset || poison_zip_u16(end + 16u) != 0u ||
               storage_file_tell(file) != archive_size || central_count != member_count) {
                result = PoisonPackageArchiveInvalid;
                goto archive_cleanup;
            }
            break;
        }
        if(signature != ZIP_CENTRAL_HEADER || central_count >= member_count) {
            result = PoisonPackageArchiveInvalid;
            goto archive_cleanup;
        }
        uint8_t header[42u];
        if(!poison_file_read_exact(file, header, sizeof(header))) goto archive_cleanup;
        const uint16_t flags = poison_zip_u16(header + 4u);
        const uint16_t compression = poison_zip_u16(header + 6u);
        const uint32_t crc32 = poison_zip_u32(header + 12u);
        const uint32_t compressed = poison_zip_u32(header + 16u);
        const uint32_t uncompressed = poison_zip_u32(header + 20u);
        const uint16_t name_length = poison_zip_u16(header + 24u);
        const uint16_t extra_length = poison_zip_u16(header + 26u);
        const uint16_t comment_length = poison_zip_u16(header + 28u);
        const uint16_t disk = poison_zip_u16(header + 30u);
        const uint32_t local_offset = poison_zip_u32(header + 38u);
        if(flags != 0u || compression != 0u || compressed != uncompressed || disk != 0u ||
           name_length == 0u || name_length > POISON_PACKAGE_PAYLOAD_PATH_MAX ||
           storage_file_tell(file) + name_length + extra_length + comment_length > archive_size) {
            result = PoisonPackageArchiveInvalid;
            goto archive_cleanup;
        }
        char path[POISON_PACKAGE_PAYLOAD_PATH_MAX + 1u];
        if(!poison_file_read_exact(file, path, name_length)) goto archive_cleanup;
        path[name_length] = '\0';
        PoisonZipMember* member = poison_package_find_member(members, member_count, path);
        if(!member || member->central_seen || member->local_offset != local_offset ||
           member->size != uncompressed || member->crc32 != crc32 ||
           !storage_file_seek(file, extra_length + comment_length, false)) {
            result = PoisonPackageArchiveInvalid;
            goto archive_cleanup;
        }
        member->central_seen = true;
        ++central_count;
    }

    result = poison_package_parse_manifest(
        manifest,
        manifest_length,
        authorities,
        firmware_api_major,
        firmware_api_minor,
        installed_version,
        verified);
    if(result != PoisonPackageArchiveOk) goto archive_cleanup;
    verified->archive_bytes = (uint32_t)archive_size;
    memcpy(verified->archive_sha256, expected_archive_sha256, 65u);
    if(!poison_package_verify_members(file, members, member_count, verified))
        result = PoisonPackageArchiveDigestMismatch;

archive_cleanup:
    if(manifest) {
        memset(manifest, 0, POISON_PACKAGE_MANIFEST_MAX_BYTES + 1u);
        free(manifest);
    }
    free(members);
cleanup:
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    if(result != PoisonPackageArchiveOk) memset(verified, 0, sizeof(*verified));
    return result;
}

static bool poison_package_directory_exists(Storage* storage, const char* path) {
    FileInfo info;
    return storage_common_stat(storage, path, &info) == FSE_OK &&
           (info.flags & FSF_DIRECTORY) != 0u;
}

static bool poison_package_mkdir_tree(Storage* storage, const char* path) {
    if(!storage || !path || path[0] != '/' || strlen(path) > POISON_PACKAGE_PAYLOAD_PATH_MAX)
        return false;
    char current[POISON_PACKAGE_PAYLOAD_PATH_MAX + 1u];
    strcpy(current, path);
    for(char* cursor = current + 1u; *cursor; ++cursor) {
        if(*cursor != '/') continue;
        *cursor = '\0';
        if(!poison_package_directory_exists(storage, current) &&
           storage_common_mkdir(storage, current) != FSE_OK) {
            return false;
        }
        *cursor = '/';
    }
    return poison_package_directory_exists(storage, current) ||
           storage_common_mkdir(storage, current) == FSE_OK;
}

static bool poison_package_extract_member(
    Storage* storage,
    File* archive,
    uint32_t data_offset,
    uint32_t size,
    const PoisonPackagePayloadDescriptor* payload,
    const char* destination_root) {
    char destination[POISON_PACKAGE_PAYLOAD_PATH_MAX + 1u];
    char partial[POISON_PACKAGE_PAYLOAD_PATH_MAX + 1u];
    if(snprintf(destination, sizeof(destination), "%s/%s", destination_root, payload->path) >=
           (int)sizeof(destination) ||
       snprintf(partial, sizeof(partial), "%s.partial", destination) >= (int)sizeof(partial)) {
        return false;
    }
    char parent[POISON_PACKAGE_PAYLOAD_PATH_MAX + 1u];
    strcpy(parent, destination);
    char* separator = strrchr(parent, '/');
    if(!separator || separator == parent) return false;
    *separator = '\0';
    if(!poison_package_mkdir_tree(storage, parent)) return false;
    (void)storage_simply_remove(storage, partial);
    File* output = storage_file_alloc(storage);
    bool ok = storage_file_seek(archive, data_offset, true) &&
              storage_file_open(output, partial, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    uint8_t buffer[256u];
    uint32_t remaining = size;
    while(ok && remaining > 0u) {
        const size_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        ok = poison_file_read_exact(archive, buffer, chunk) &&
             storage_file_write(output, buffer, chunk) == chunk;
        remaining -= ok ? (uint32_t)chunk : 0u;
    }
    ok = ok && storage_file_close(output) &&
         storage_common_rename(storage, partial, destination) == FSE_OK;
    storage_file_free(output);
    memset(buffer, 0, sizeof(buffer));
    if(!ok) (void)storage_simply_remove(storage, partial);
    return ok;
}

bool poison_package_extract_verified_archive(
    const char* archive_path,
    const PoisonPackageVerifiedArchive* verified,
    const char* destination_root) {
    if(!archive_path || !verified || !destination_root || verified->payload_count == 0u ||
       verified->payload_count > POISON_PACKAGE_PAYLOAD_MAX || destination_root[0] != '/' ||
       strlen(destination_root) > 160u) {
        return false;
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* archive = storage_file_alloc(storage);
    bool ok = storage_file_open(archive, archive_path, FSAM_READ, FSOM_OPEN_EXISTING) &&
              storage_file_size(archive) == verified->archive_bytes &&
              storage_file_seek(archive, 0u, true);
    uint8_t archive_digest[32u];
    char archive_hex[65u];
    ok = ok && poison_package_hash_file(archive, verified->archive_bytes, archive_digest);
    if(ok) {
        poison_package_hex_encode(archive_digest, archive_hex);
        ok = strcmp(archive_hex, verified->archive_sha256) == 0;
    }
    if(ok) {
        (void)storage_simply_remove_recursive(storage, destination_root);
        ok = poison_package_mkdir_tree(storage, destination_root) &&
             storage_file_seek(archive, 0u, true);
    }
    bool extracted[POISON_PACKAGE_PAYLOAD_MAX] = {0};
    size_t extracted_count = 0u;
    while(ok && storage_file_tell(archive) + 4u <= verified->archive_bytes) {
        uint8_t signature_bytes[4u];
        ok = poison_file_read_exact(archive, signature_bytes, sizeof(signature_bytes));
        if(!ok) break;
        const uint32_t signature = poison_zip_u32(signature_bytes);
        if(signature == ZIP_CENTRAL_HEADER) break;
        if(signature != ZIP_LOCAL_HEADER) {
            ok = false;
            break;
        }
        uint8_t header[26u];
        ok = poison_file_read_exact(archive, header, sizeof(header));
        if(!ok) break;
        const uint16_t flags = poison_zip_u16(header + 2u);
        const uint16_t compression = poison_zip_u16(header + 4u);
        const uint32_t compressed = poison_zip_u32(header + 14u);
        const uint32_t uncompressed = poison_zip_u32(header + 18u);
        const uint16_t name_length = poison_zip_u16(header + 22u);
        const uint16_t extra_length = poison_zip_u16(header + 24u);
        if(flags != 0u || compression != 0u || compressed != uncompressed || name_length == 0u ||
           name_length > POISON_PACKAGE_PAYLOAD_PATH_MAX || extra_length > 1024u) {
            ok = false;
            break;
        }
        char path[POISON_PACKAGE_PAYLOAD_PATH_MAX + 1u];
        ok = poison_file_read_exact(archive, path, name_length);
        if(!ok) break;
        path[name_length] = '\0';
        ok = poison_package_path_valid(path) && storage_file_seek(archive, extra_length, false);
        if(!ok) break;
        const uint32_t data_offset = (uint32_t)storage_file_tell(archive);
        if(strcmp(path, "manifest.json") == 0) {
            ok = storage_file_seek(archive, uncompressed, false);
            continue;
        }
        size_t payload_index = verified->payload_count;
        for(size_t index = 0; index < verified->payload_count; ++index) {
            if(strcmp(path, verified->payloads[index].path) == 0) {
                payload_index = index;
                break;
            }
        }
        if(payload_index == verified->payload_count || extracted[payload_index] ||
           verified->payloads[payload_index].size != uncompressed ||
           !poison_package_extract_member(
               storage,
               archive,
               data_offset,
               uncompressed,
               &verified->payloads[payload_index],
               destination_root)) {
            ok = false;
            break;
        }
        extracted[payload_index] = true;
        ++extracted_count;
        ok = storage_file_seek(archive, data_offset + uncompressed, true);
    }
    ok = ok && extracted_count == verified->payload_count;
    storage_file_free(archive);
    if(!ok) (void)storage_simply_remove_recursive(storage, destination_root);
    furi_record_close(RECORD_STORAGE);
    memset(archive_digest, 0, sizeof(archive_digest));
    memset(archive_hex, 0, sizeof(archive_hex));
    return ok;
}
