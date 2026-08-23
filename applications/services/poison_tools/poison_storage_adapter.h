#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../rpc/poison_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_STORAGE_READ_MAX 1024u

typedef struct {
    uint64_t file_size;
    size_t bytes_read;
    bool end_of_file;
} PoisonStorageReadResult;

typedef bool (*PoisonStorageCancelCallback)(void* context);

bool poison_storage_read(
    const char* logical_path,
    PoisonRole role,
    uint32_t offset,
    uint8_t* output,
    size_t capacity,
    PoisonStorageReadResult* result);
bool poison_storage_sha256(const char* logical_path, PoisonRole role, char digest_hex[65]);
bool poison_storage_sha256_cancellable(
    const char* logical_path,
    PoisonRole role,
    char digest_hex[65],
    PoisonStorageCancelCallback cancelled,
    void* context);

#ifdef __cplusplus
}
#endif
