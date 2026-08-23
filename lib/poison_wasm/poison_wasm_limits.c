#include "poison_wasm_limits.h"

#include <string.h>

void poison_wasm_limits_init(PoisonWasmLimits* limits, uint32_t fuel, uint32_t wall_ms, uint16_t handles, uint32_t logs, uint16_t artifacts) {
    if(!limits) return;
    memset(limits, 0, sizeof(*limits));
    limits->fuel_limit = fuel;
    limits->wall_limit_ms = wall_ms;
    limits->handle_limit = handles;
    limits->log_limit = logs;
    limits->artifact_limit = artifacts;
}

bool poison_wasm_limits_consume(PoisonWasmLimits* limits, uint32_t fuel, uint32_t wall_ms, uint16_t handles, uint32_t logs, uint16_t artifacts) {
    if(!limits || limits->tripped || fuel > limits->fuel_limit - (limits->fuel < limits->fuel_limit ? limits->fuel : limits->fuel_limit) ||
       wall_ms > limits->wall_limit_ms - (limits->wall_ms < limits->wall_limit_ms ? limits->wall_ms : limits->wall_limit_ms) ||
       handles > limits->handle_limit - (limits->handles < limits->handle_limit ? limits->handles : limits->handle_limit) ||
       logs > limits->log_limit - (limits->logs < limits->log_limit ? limits->logs : limits->log_limit) ||
       artifacts > limits->artifact_limit - (limits->artifacts < limits->artifact_limit ? limits->artifacts : limits->artifact_limit)) {
        if(limits) limits->tripped = true;
        return false;
    }
    limits->fuel += fuel;
    limits->wall_ms += wall_ms;
    limits->handles += handles;
    limits->logs += logs;
    limits->artifacts += artifacts;
    return true;
}
