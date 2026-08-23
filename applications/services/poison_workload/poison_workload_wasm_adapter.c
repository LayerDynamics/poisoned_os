#include "poison_workload_wasm_adapter.h"

#include <lib/poison_wasm/poison_wasm_host.h>

bool poison_workload_wasm_admit(
    PoisonWorkloadWasmAdapter* adapter,
    const uint8_t* bytes,
    size_t size,
    uint32_t capability_mask) {
    if(!adapter || capability_mask == 0u) return false;
    adapter->admitted = poison_wasm_admit(bytes, size, &adapter->module) == PoisonWasmOk;
    adapter->capability_mask = capability_mask;
    return adapter->admitted;
}

PoisonWasmResult poison_workload_wasm_start(PoisonWorkloadWasmAdapter* adapter) {
    if(!adapter || !adapter->admitted) return PoisonWasmInvalidModule;
    return poison_wasm_host_start(&adapter->module, adapter->capability_mask);
}

void poison_workload_wasm_cleanup(PoisonWorkloadWasmAdapter* adapter) {
    if(!adapter) return;
    adapter->module = (PoisonWasmModule){0};
    adapter->capability_mask = 0u;
    adapter->admitted = false;
}
