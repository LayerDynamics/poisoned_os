#include "poison_rust_api.h"

#include <applications/services/poison_startup.h>

#include <string.h>

void poison_rust_api_on_system_start(void) {
    if(!poison_startup_is_runtime_boot()) return;
}

bool poison_rust_byte_slice_valid(PoisonRustByteSlice slice, size_t maximum) {
    return (slice.len == 0u || slice.ptr != NULL) && slice.len <= maximum;
}

PoisonRustStatus poison_rust_validate_path(PoisonRustByteSlice path) {
    if(!poison_rust_byte_slice_valid(path, POISON_RUST_MAX_PATH) || path.len == 0u) {
        return PoisonRustStatusInvalidArgument;
    }
    for(size_t i = 0; i < path.len; i++) {
        const uint8_t byte = path.ptr[i];
        if(byte == 0u || byte == '\\' || (i == 0u && byte != '/')) {
            return PoisonRustStatusInvalidArgument;
        }
    }
    return PoisonRustStatusOk;
}

PoisonRustStatus poison_rust_check_capability(uint32_t capability, bool granted) {
    if(capability == 0u) return PoisonRustStatusInvalidArgument;
    return granted ? PoisonRustStatusOk : PoisonRustStatusDenied;
}
