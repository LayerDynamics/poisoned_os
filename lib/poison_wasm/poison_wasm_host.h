#pragma once

#include "poison_wasm.h"

#ifdef __cplusplus
extern "C" {
#endif

bool poison_wasm_host_import_allowed(const char* import_name, uint32_t capability_mask);
PoisonWasmResult poison_wasm_host_start(const PoisonWasmModule* module, uint32_t capability_mask);

#ifdef __cplusplus
}
#endif
