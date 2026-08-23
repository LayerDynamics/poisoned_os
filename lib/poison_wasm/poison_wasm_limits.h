#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t fuel;
    uint32_t fuel_limit;
    uint32_t wall_ms;
    uint32_t wall_limit_ms;
    uint16_t handles;
    uint16_t handle_limit;
    uint32_t logs;
    uint32_t log_limit;
    uint16_t artifacts;
    uint16_t artifact_limit;
    bool tripped;
} PoisonWasmLimits;

void poison_wasm_limits_init(PoisonWasmLimits* limits, uint32_t fuel, uint32_t wall_ms, uint16_t handles, uint32_t logs, uint16_t artifacts);
bool poison_wasm_limits_consume(PoisonWasmLimits* limits, uint32_t fuel, uint32_t wall_ms, uint16_t handles, uint32_t logs, uint16_t artifacts);
