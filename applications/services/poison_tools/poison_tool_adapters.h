#pragma once

#include <stdbool.h>

typedef enum {
    PoisonToolAdapterNfc,
    PoisonToolAdapterLfRfid,
    PoisonToolAdapterIbutton,
    PoisonToolAdapterInfrared,
    PoisonToolAdapterSubGhz,
    PoisonToolAdapterGpio,
    PoisonToolAdapterUsbHid,
    PoisonToolAdapterBleHid,
    PoisonToolAdapterSerial,
    PoisonToolAdapterStorage,
    PoisonToolAdapterMarauder,
    PoisonToolAdapterEspFlasher,
} PoisonToolAdapter;

#ifdef __cplusplus
extern "C" {
#endif

bool poison_tool_adapter_for_family(const char* family, PoisonToolAdapter* adapter);
bool poison_tool_adapter_supports_capability(PoisonToolAdapter adapter, const char* capability);

#ifdef __cplusplus
}
#endif
