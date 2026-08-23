#include "poison_js_bundle_i.h"

#include "../poison_packages/poison_package_archive.h"
#include "../poison_packages/poison_package_manager.h"

#include <furi.h>
#include <loader/firmware_api/firmware_api.h>
#include <storage/storage.h>

#include <mbedtls/sha256.h>

#include <stdio.h>
#include <string.h>

bool poison_js_bundle_digest_valid(const char* digest) {
    if(!digest || strnlen(digest, POISON_JS_BUNDLE_MAX_DIGEST) != POISON_JS_BUNDLE_MAX_DIGEST - 1u)
        return false;
    for(size_t i = 0; i < POISON_JS_BUNDLE_MAX_DIGEST - 1u; ++i) {
        const char c = digest[i];
        if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool poison_js_bundle_path_valid(const char* path) {
    if(!path || path[0] == '\0' || path[0] == '/' ||
       strnlen(path, POISON_JS_BUNDLE_MAX_PATH + 1u) > POISON_JS_BUNDLE_MAX_PATH) {
        return false;
    }
    const char* segment = path;
    for(const char* cursor = path;; ++cursor) {
        const unsigned char byte = (unsigned char)*cursor;
        if(byte == '\0' || byte == '/') {
            const size_t segment_length = (size_t)(cursor - segment);
            if(segment_length == 0u || (segment_length == 1u && segment[0] == '.') ||
               (segment_length == 2u && segment[0] == '.' && segment[1] == '.')) {
                return false;
            }
            if(byte == '\0') break;
            segment = cursor + 1u;
        } else if(byte < 0x20u || byte == 0x7fu || byte == '\\') {
            return false;
        }
    }
    return true;
}

bool poison_js_bundle_metadata_valid(const PoisonJsBundleMetadata* metadata) {
    if(!metadata || metadata->id[0] == '\0' || metadata->version[0] == '\0' ||
       strnlen(metadata->id, sizeof(metadata->id)) == sizeof(metadata->id) ||
       strnlen(metadata->version, sizeof(metadata->version)) == sizeof(metadata->version) ||
       strnlen(metadata->entrypoint, sizeof(metadata->entrypoint)) ==
           sizeof(metadata->entrypoint) ||
       metadata->api_version == 0u || metadata->size == 0u ||
       metadata->size > POISON_JS_BUNDLE_MAX_BYTES ||
       metadata->capability_count > POISON_JS_BUNDLE_MAX_CAPABILITIES ||
       !poison_js_bundle_path_valid(metadata->entrypoint) ||
       !poison_js_bundle_digest_valid(metadata->content_digest))
        return false;
    for(const char* cursor = metadata->id; *cursor; ++cursor) {
        if(!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= '0' && *cursor <= '9') ||
             *cursor == '.' || *cursor == '-' || *cursor == '_'))
            return false;
    }
    return true;
}

static bool poison_js_bundle_id_valid(const char* value) {
    if(!value || value[0] == '\0' ||
       strnlen(value, POISON_JS_BUNDLE_MAX_ID + 1u) > POISON_JS_BUNDLE_MAX_ID)
        return false;
    for(const char* cursor = value; *cursor; ++cursor) {
        if(!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= '0' && *cursor <= '9') ||
             *cursor == '.' || *cursor == '-' || *cursor == '_')) {
            return false;
        }
    }
    return true;
}

static void poison_js_bundle_hex_encode(const uint8_t input[32u], char output[65u]) {
    static const char hex[] = "0123456789abcdef";
    for(size_t index = 0u; index < 32u; ++index) {
        output[index * 2u] = hex[input[index] >> 4u];
        output[index * 2u + 1u] = hex[input[index] & 0x0fu];
    }
    output[64u] = '\0';
}

static bool poison_js_bundle_active_path(
    const PoisonJsBundle* bundle,
    const char* relative_path,
    char output[POISON_JS_BUNDLE_MAX_PATH + 1u]) {
    if(!bundle || !poison_js_bundle_path_valid(relative_path)) return false;
    const int written = snprintf(
        output, POISON_JS_BUNDLE_MAX_PATH + 1u, "%s/%s", bundle->active_root, relative_path);
    return written > 0 && written <= (int)POISON_JS_BUNDLE_MAX_PATH;
}

static bool poison_js_bundle_verify_active_payloads(PoisonJsBundle* bundle) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    uint8_t* buffer = malloc(256u);
    mbedtls_sha256_context content_hash;
    mbedtls_sha256_init(&content_hash);
    bool valid = buffer && mbedtls_sha256_starts(&content_hash, 0) == 0;
    size_t order[POISON_JS_BUNDLE_MAX_ASSETS];
    for(size_t index = 0u; index < bundle->asset_count; ++index) {
        order[index] = index;
        size_t cursor = index;
        while(cursor > 0u &&
              strcmp(bundle->assets[order[cursor - 1u]].path, bundle->assets[order[cursor]].path) >
                  0) {
            const size_t prior = order[cursor - 1u];
            order[cursor - 1u] = order[cursor];
            order[cursor] = prior;
            --cursor;
        }
    }
    for(size_t index = 0u; valid && index < bundle->asset_count; ++index) {
        const PoisonJsBundleAsset* asset = &bundle->assets[order[index]];
        char path[POISON_JS_BUNDLE_MAX_PATH + 1u];
        FileInfo info;
        valid = poison_js_bundle_active_path(bundle, asset->path, path) &&
                storage_common_stat(storage, path, &info) == FSE_OK &&
                (info.flags & FSF_DIRECTORY) == 0u && info.size == asset->size &&
                storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
                mbedtls_sha256_update(
                    &content_hash, (const uint8_t*)asset->path, strlen(asset->path)) == 0;
        const uint8_t separator = 0u;
        valid = valid && mbedtls_sha256_update(&content_hash, &separator, 1u) == 0;
        mbedtls_sha256_context asset_hash;
        mbedtls_sha256_init(&asset_hash);
        valid = valid && mbedtls_sha256_starts(&asset_hash, 0) == 0;
        uint32_t remaining = asset->size;
        while(valid && remaining > 0u) {
            const size_t requested = remaining > 256u ? 256u : remaining;
            const size_t received = storage_file_read(file, buffer, requested);
            valid = received == requested &&
                    mbedtls_sha256_update(&asset_hash, buffer, received) == 0 &&
                    mbedtls_sha256_update(&content_hash, buffer, received) == 0;
            remaining -= valid ? (uint32_t)received : 0u;
        }
        uint8_t digest[32u];
        char digest_hex[65u];
        valid = valid && mbedtls_sha256_finish(&asset_hash, digest) == 0;
        mbedtls_sha256_free(&asset_hash);
        if(valid) {
            poison_js_bundle_hex_encode(digest, digest_hex);
            valid = strcmp(digest_hex, asset->sha256) == 0;
        }
        memset(digest, 0, sizeof(digest));
        memset(digest_hex, 0, sizeof(digest_hex));
        if(storage_file_is_open(file)) valid = storage_file_close(file) && valid;
    }
    uint8_t content_digest[32u];
    char content_hex[65u];
    valid = valid && mbedtls_sha256_finish(&content_hash, content_digest) == 0;
    mbedtls_sha256_free(&content_hash);
    if(valid) {
        poison_js_bundle_hex_encode(content_digest, content_hex);
        valid = strcmp(content_hex, bundle->metadata.content_digest) == 0;
    }
    memset(content_digest, 0, sizeof(content_digest));
    memset(content_hex, 0, sizeof(content_hex));
    if(buffer) {
        memset(buffer, 0, 256u);
        free(buffer);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return valid;
}

bool poison_js_bundle_still_active(const PoisonJsBundle* bundle) {
    if(!bundle || !poison_js_bundle_metadata_valid(&bundle->metadata)) return false;
    const PoisonPackageManager* manager = poison_packages_manager();
    const PoisonPackageRecord* record = poison_package_manager_find(manager, bundle->metadata.id);
    return manager->generation == bundle->package_generation && record &&
           poison_package_manager_active_content(manager, bundle->metadata.id, "ui-pack") &&
           strcmp(record->version, bundle->metadata.version) == 0 &&
           record->capability_mask == bundle->capability_mask;
}

bool poison_js_bundle_load_verified(
    PoisonJsBundle* bundle,
    const char* bundle_id,
    const char* version,
    const char* content_sha256,
    uint64_t granted_capabilities) {
    if(!bundle || !poison_js_bundle_id_valid(bundle_id) || !version || version[0] == '\0' ||
       strnlen(version, POISON_JS_BUNDLE_MAX_VERSION + 1u) > POISON_JS_BUNDLE_MAX_VERSION ||
       !poison_js_bundle_digest_valid(content_sha256)) {
        return false;
    }
    memset(bundle, 0, sizeof(*bundle));
    const PoisonPackageManager* manager = poison_packages_manager();
    const uint32_t package_generation = manager->generation;
    const PoisonPackageRecord* record = poison_package_manager_find(manager, bundle_id);
    if(!record || !poison_package_manager_active_content(manager, bundle_id, "ui-pack") ||
       strcmp(record->version, version) != 0 ||
       (record->capability_mask & ~granted_capabilities) != 0u) {
        return false;
    }
    const PoisonPackageRecord record_snapshot = *record;
    if(manager->generation != package_generation) return false;
    PoisonPackageStorageLayout layout;
    poison_package_storage_layout_default(&layout);
    const int active_root_written = snprintf(
        bundle->active_root,
        sizeof(bundle->active_root),
        "%s/%s",
        layout.active_root,
        record_snapshot.package_id);
    if(active_root_written <= 0 || active_root_written >= (int)sizeof(bundle->active_root)) {
        memset(bundle, 0, sizeof(*bundle));
        return false;
    }
    PoisonPackageVerifiedArchive* verified = malloc(sizeof(*verified));
    if(!verified) {
        memset(bundle, 0, sizeof(*bundle));
        return false;
    }
    const PoisonPackageArchiveResult result = poison_package_verify_installed_manifest(
        bundle->active_root,
        poison_packages_authorities(),
        firmware_api_interface->api_version_major,
        firmware_api_interface->api_version_minor,
        verified);
    bool valid =
        result == PoisonPackageArchiveOk && strcmp(verified->content_type, "ui-pack") == 0 &&
        strcmp(verified->package_id, record_snapshot.package_id) == 0 &&
        strcmp(verified->version, record_snapshot.version) == 0 &&
        strcmp(verified->signing_key_id, record_snapshot.signing_key_id) == 0 &&
        strcmp(verified->content_sha256, content_sha256) == 0 &&
        verified->capability_mask == record_snapshot.capability_mask &&
        verified->capability_count <= POISON_JS_BUNDLE_MAX_CAPABILITIES &&
        verified->payload_count > 0u && verified->payload_count <= POISON_JS_BUNDLE_MAX_ASSETS &&
        poison_js_bundle_path_valid(verified->entrypoint) &&
        strnlen(verified->entrypoint, sizeof(verified->entrypoint)) <=
            POISON_JS_BUNDLE_MAX_ENTRYPOINT;
    uint64_t total = 0u;
    if(valid) {
        strcpy(bundle->metadata.id, verified->package_id);
        strcpy(bundle->metadata.version, verified->version);
        strcpy(bundle->metadata.entrypoint, verified->entrypoint);
        strcpy(bundle->metadata.content_digest, verified->content_sha256);
        bundle->metadata.api_version = firmware_api_interface->api_version_major;
        bundle->metadata.capability_count = (uint8_t)verified->capability_count;
        bundle->capability_mask = verified->capability_mask;
        bundle->package_generation = package_generation;
        bundle->asset_count = verified->payload_count;
        for(size_t index = 0u; valid && index < verified->capability_count; ++index) {
            valid = verified->capabilities[index][0] != '\0';
            if(valid) strcpy(bundle->capabilities[index], verified->capabilities[index]);
        }
        for(size_t index = 0u; valid && index < verified->payload_count; ++index) {
            const PoisonPackagePayloadDescriptor* source = &verified->payloads[index];
            valid = poison_js_bundle_path_valid(source->path) &&
                    poison_js_bundle_digest_valid(source->sha256) &&
                    total + source->size <= POISON_JS_BUNDLE_MAX_BYTES;
            if(valid) {
                strcpy(bundle->assets[index].path, source->path);
                strcpy(bundle->assets[index].sha256, source->sha256);
                bundle->assets[index].size = source->size;
                total += source->size;
            }
        }
        bundle->metadata.size = (uint32_t)total;
        const PoisonPackageRecord* current = poison_package_manager_find(manager, bundle_id);
        valid = manager->generation == package_generation && current &&
                poison_package_manager_active_content(manager, bundle_id, "ui-pack") &&
                strcmp(current->version, record_snapshot.version) == 0 &&
                strcmp(current->digest, record_snapshot.digest) == 0 &&
                strcmp(current->signing_key_id, record_snapshot.signing_key_id) == 0 &&
                current->capability_mask == record_snapshot.capability_mask;
        valid = valid && poison_js_bundle_metadata_valid(&bundle->metadata) &&
                poison_js_bundle_find_asset(bundle, bundle->metadata.entrypoint) != NULL &&
                poison_js_bundle_verify_active_payloads(bundle) &&
                poison_js_bundle_still_active(bundle);
    }
    memset(verified, 0, sizeof(*verified));
    free(verified);
    if(!valid) memset(bundle, 0, sizeof(*bundle));
    return valid;
}

const PoisonJsBundleAsset*
    poison_js_bundle_find_asset(const PoisonJsBundle* bundle, const char* asset_path) {
    if(!bundle || !poison_js_bundle_path_valid(asset_path)) return NULL;
    for(size_t index = 0u; index < bundle->asset_count; ++index) {
        if(strcmp(bundle->assets[index].path, asset_path) == 0) return &bundle->assets[index];
    }
    return NULL;
}

bool poison_js_bundle_read_asset(
    const PoisonJsBundle* bundle,
    const char* asset_path,
    uint32_t offset,
    uint8_t* output,
    size_t output_capacity,
    size_t* output_size,
    bool* eof) {
    if(output_size) *output_size = 0u;
    if(eof) *eof = false;
    const PoisonJsBundleAsset* asset = poison_js_bundle_find_asset(bundle, asset_path);
    if(!asset || !output || output_capacity == 0u ||
       output_capacity > POISON_JS_BUNDLE_MAX_READ_BYTES || !output_size || !eof ||
       offset > asset->size || !poison_js_bundle_still_active(bundle)) {
        return false;
    }
    char path[POISON_JS_BUNDLE_MAX_PATH + 1u];
    if(!poison_js_bundle_active_path(bundle, asset_path, path)) return false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    FileInfo info;
    bool valid = storage_common_stat(storage, path, &info) == FSE_OK &&
                 (info.flags & FSF_DIRECTORY) == 0u && info.size == asset->size &&
                 storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
                 storage_file_seek(file, offset, true);
    const size_t remaining = asset->size - offset;
    const size_t requested = remaining < output_capacity ? remaining : output_capacity;
    size_t received = 0u;
    if(valid && requested > 0u) received = storage_file_read(file, output, requested);
    valid = valid && received == requested && !storage_file_get_error(file);
    if(valid) {
        *output_size = received;
        *eof = offset + received == asset->size;
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return valid;
}
