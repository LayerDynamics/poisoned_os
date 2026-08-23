#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_RUST_API_VERSION 1u
#define POISON_RUST_ABI_VERSION 1u
#define POISON_RUST_MAX_PATH    128u
#define POISON_RUST_MAX_CHUNK   1024u
#define POISON_RUST_MAX_TEXT    256u

typedef enum {
    PoisonRustStatusOk = 0,
    PoisonRustStatusInvalidArgument = 1,
    PoisonRustStatusDenied = 2,
    PoisonRustStatusCancelled = 3,
    PoisonRustStatusResourceLimit = 4,
} PoisonRustStatus;

typedef struct {
    const uint8_t* ptr;
    size_t len;
} PoisonRustByteSlice;

void poison_rust_api_on_system_start(void);
bool poison_rust_byte_slice_valid(PoisonRustByteSlice slice, size_t maximum);
PoisonRustStatus poison_rust_validate_path(PoisonRustByteSlice path);
PoisonRustStatus poison_rust_check_capability(uint32_t capability, bool granted);

#ifdef __cplusplus
}
#endif
