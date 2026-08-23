#include "poison_storage_adapter.h"

#include "../poison_vfs/poison_vfs_paths.h"

#include <furi.h>
#include <mbedtls/sha256.h>
#include <storage/storage.h>
#include <stdio.h>
#include <string.h>

#define POISON_STORAGE_HASH_MAX (16u * 1024u * 1024u)

static bool poison_storage_open_logical(
    const char* logical_path,
    PoisonRole role,
    Storage** storage,
    File** file,
    uint64_t* size) {
    PoisonVfsResolvedPath resolved;
    if(!poison_vfs_resolve_path(logical_path, role, PoisonVfsOperationRead, &resolved))
        return false;
    *storage = furi_record_open(RECORD_STORAGE);
    *file = storage_file_alloc(*storage);
    if(!*file || !storage_file_open(*file, resolved.backing_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        if(*file) storage_file_free(*file);
        furi_record_close(RECORD_STORAGE);
        *storage = NULL;
        *file = NULL;
        return false;
    }
    *size = storage_file_size(*file);
    return true;
}

static void poison_storage_close(Storage* storage, File* file) {
    if(file) {
        storage_file_close(file);
        storage_file_free(file);
    }
    if(storage) furi_record_close(RECORD_STORAGE);
}

bool poison_storage_read(
    const char* logical_path,
    PoisonRole role,
    uint32_t offset,
    uint8_t* output,
    size_t capacity,
    PoisonStorageReadResult* result) {
    if(!output || capacity == 0u || capacity > POISON_STORAGE_READ_MAX || !result) return false;
    Storage* storage = NULL;
    File* file = NULL;
    uint64_t size = 0u;
    if(!poison_storage_open_logical(logical_path, role, &storage, &file, &size)) return false;
    bool ok = offset <= size && storage_file_seek(file, offset, true);
    memset(result, 0, sizeof(*result));
    result->file_size = size;
    if(ok) {
        result->bytes_read = storage_file_read(file, output, capacity);
        result->end_of_file = (uint64_t)offset + result->bytes_read >= size;
    }
    poison_storage_close(storage, file);
    return ok;
}

bool poison_storage_sha256_cancellable(
    const char* logical_path,
    PoisonRole role,
    char digest_hex[65],
    PoisonStorageCancelCallback cancelled,
    void* context) {
    if(!digest_hex) return false;
    Storage* storage = NULL;
    File* file = NULL;
    uint64_t size = 0u;
    if(!poison_storage_open_logical(logical_path, role, &storage, &file, &size) ||
       size > POISON_STORAGE_HASH_MAX) {
        poison_storage_close(storage, file);
        return false;
    }
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool ok = mbedtls_sha256_starts(&hash, 0) == 0;
    uint8_t buffer[512];
    uint64_t remaining = size;
    while(ok && remaining) {
        if(cancelled && cancelled(context)) {
            ok = false;
            break;
        }
        const size_t requested = remaining < sizeof(buffer) ? (size_t)remaining : sizeof(buffer);
        const size_t read = storage_file_read(file, buffer, requested);
        ok = read == requested && mbedtls_sha256_update(&hash, buffer, read) == 0;
        remaining -= read;
    }
    uint8_t digest[32];
    if(ok) ok = mbedtls_sha256_finish(&hash, digest) == 0;
    mbedtls_sha256_free(&hash);
    poison_storage_close(storage, file);
    if(!ok) return false;
    for(size_t index = 0u; index < sizeof(digest); index++)
        snprintf(digest_hex + index * 2u, 3u, "%02x", digest[index]);
    digest_hex[64] = '\0';
    return true;
}

bool poison_storage_sha256(const char* logical_path, PoisonRole role, char digest_hex[65]) {
    return poison_storage_sha256_cancellable(logical_path, role, digest_hex, NULL, NULL);
}
