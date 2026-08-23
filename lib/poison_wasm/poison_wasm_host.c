#include "poison_wasm_host.h"

#include <string.h>

bool poison_wasm_host_import_allowed(const char* import_name, uint32_t capability_mask) {
    if(!import_name || strncmp(import_name, "poison_", 7u) != 0 || capability_mask == 0u) return false;
    return strlen(import_name) <= 64u;
}

PoisonWasmResult poison_wasm_host_start(const PoisonWasmModule* module, uint32_t capability_mask) {
    if(!module || !module->bytes || !poison_wasm_host_import_allowed("poison_runtime", capability_mask)) return PoisonWasmInvalidModule;
    return poison_wasm_runtime_available() ? PoisonWasmOk : PoisonWasmRuntimeUnavailable;
}
