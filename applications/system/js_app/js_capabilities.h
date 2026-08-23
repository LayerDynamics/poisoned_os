#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JsCapabilityDevice = 1u << 0,
    JsCapabilityRuntime = 1u << 1,
    JsCapabilityUi = 1u << 2,
    JsCapabilityNotification = 1u << 3,
    JsCapabilityBadUsb = 1u << 4,
    JsCapabilitySerial = 1u << 5,
    JsCapabilityGpio = 1u << 6,
    JsCapabilityStorage = 1u << 7,
    JsCapabilityCrypto = 1u << 8,
    JsCapabilityCompute = 1u << 9,
    JsCapabilityEvidence = 1u << 10,
} JsCapability;

bool js_capability_module_known(const char* module);
uint32_t js_capability_for_module(const char* module);
bool js_capability_module_allowed(const char* module, uint32_t granted);

#ifdef __cplusplus
}
#endif
