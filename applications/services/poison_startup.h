#pragma once

#include <furi_hal.h>

static inline bool poison_startup_is_runtime_boot(void) {
    return furi_hal_rtc_get_boot_mode() == FuriHalRtcBootModeNormal;
}
