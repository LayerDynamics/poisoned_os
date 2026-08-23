#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lib/poison_wasm/poison_wasm.h>

typedef struct {
    PoisonWasmModule module;
    uint32_t capability_mask;
    bool admitted;
} PoisonWorkloadWasmAdapter;

bool poison_workload_wasm_admit(
    PoisonWorkloadWasmAdapter* adapter,
    const uint8_t* bytes,
    size_t size,
    uint32_t capability_mask);
PoisonWasmResult poison_workload_wasm_start(PoisonWorkloadWasmAdapter* adapter);
void poison_workload_wasm_cleanup(PoisonWorkloadWasmAdapter* adapter);
