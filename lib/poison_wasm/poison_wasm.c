#include "poison_wasm.h"

#include <string.h>

static bool contains_forbidden_import(const uint8_t* bytes, size_t size) {
    static const char* forbidden[] = {"wasi_snapshot_preview1", "env", "memory", "fd_", "sock", "thread"};
    for(size_t i = 0; i < sizeof(forbidden) / sizeof(forbidden[0]); ++i) {
        const size_t length = strlen(forbidden[i]);
        for(size_t offset = 8; offset + length <= size; ++offset) if(memcmp(bytes + offset, forbidden[i], length) == 0) return true;
    }
    return false;
}

static bool contains_unapproved_import(const uint8_t* bytes, size_t size) {
    for(size_t offset = 8; offset + 7u <= size; ++offset) {
        if(memcmp(bytes + offset, "poison_", 7u) == 0) continue;
        if(bytes[offset] >= 'a' && bytes[offset] <= 'z' && offset > 8u && bytes[offset - 1u] == '_') return true;
    }
    return false;
}

PoisonWasmResult poison_wasm_admit(const uint8_t* bytes, size_t size, PoisonWasmModule* module) {
    if(!module || !bytes || size < 8u || size > POISON_WASM_MAX_MODULE_BYTES ||
       memcmp(bytes, "\0asm\x01\0\0\0", 8u) != 0)
        return PoisonWasmInvalidModule;
    if(contains_forbidden_import(bytes, size)) return PoisonWasmForgedImport;
    if(contains_unapproved_import(bytes, size)) return PoisonWasmForgedImport;
    memset(module, 0, sizeof(*module));
    module->bytes = bytes;
    module->size = size;
    module->import_count = 0;
    module->memory_pages = 0;
    module->has_table = false;
    return PoisonWasmOk;
}

bool poison_wasm_runtime_available(void) {
    return false;
}
