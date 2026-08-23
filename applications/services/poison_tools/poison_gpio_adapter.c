#include "poison_gpio_adapter.h"

#include <furi.h>
#include <furi_hal_gpio.h>
#include <furi_hal_resources.h>
#include <string.h>
#include <strings.h>

static const GpioPin* poison_gpio_hal_pin(PoisonGpioPin pin) {
    switch(pin) {
    case PoisonGpioPinPc0:
        return &gpio_ext_pc0;
    case PoisonGpioPinPc1:
        return &gpio_ext_pc1;
    case PoisonGpioPinPc3:
        return &gpio_ext_pc3;
    case PoisonGpioPinPb2:
        return &gpio_ext_pb2;
    case PoisonGpioPinPb3:
        return &gpio_ext_pb3;
    case PoisonGpioPinPa4:
        return &gpio_ext_pa4;
    case PoisonGpioPinPa6:
        return &gpio_ext_pa6;
    case PoisonGpioPinPa7:
        return &gpio_ext_pa7;
    default:
        return NULL;
    }
}

bool poison_gpio_pin_parse(const char* name, PoisonGpioPin* pin) {
    if(!name || !pin) return false;
    static const char* const names[] = {"PC0", "PC1", "PC3", "PB2", "PB3", "PA4", "PA6", "PA7"};
    for(size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if(strcasecmp(name, names[i]) == 0) {
            *pin = (PoisonGpioPin)i;
            return true;
        }
    }
    return false;
}

const char* poison_gpio_pin_name(PoisonGpioPin pin) {
    static const char* const names[] = {"PC0", "PC1", "PC3", "PB2", "PB3", "PA4", "PA6", "PA7"};
    return pin < (sizeof(names) / sizeof(names[0])) ? names[pin] : NULL;
}

bool poison_gpio_configure(PoisonGpioPin pin, PoisonGpioMode mode, PoisonGpioPull pull) {
    const GpioPin* hal_pin = poison_gpio_hal_pin(pin);
    if(!hal_pin || mode > PoisonGpioOutputOpenDrain || pull > PoisonGpioPullDown) return false;
    const GpioMode hal_mode = mode == PoisonGpioInput          ? GpioModeInput :
                              mode == PoisonGpioOutputPushPull ? GpioModeOutputPushPull :
                                                                 GpioModeOutputOpenDrain;
    const GpioPull hal_pull = pull == PoisonGpioPullUp   ? GpioPullUp :
                              pull == PoisonGpioPullDown ? GpioPullDown :
                                                           GpioPullNo;
    furi_hal_gpio_init(hal_pin, hal_mode, hal_pull, GpioSpeedVeryHigh);
    return true;
}

bool poison_gpio_write(PoisonGpioPin pin, bool level) {
    const GpioPin* hal_pin = poison_gpio_hal_pin(pin);
    if(!hal_pin) return false;
    furi_hal_gpio_write(hal_pin, level);
    return true;
}

bool poison_gpio_read(PoisonGpioPin pin, bool* level) {
    const GpioPin* hal_pin = poison_gpio_hal_pin(pin);
    if(!hal_pin || !level) return false;
    *level = furi_hal_gpio_read(hal_pin);
    return true;
}

bool poison_gpio_sample(
    PoisonGpioPin pin,
    uint32_t interval_ms,
    uint32_t duration_ms,
    PoisonGpioSample* result) {
    if(!result || !poison_gpio_hal_pin(pin) || interval_ms == 0u || duration_ms == 0u ||
       duration_ms > 60000u)
        return false;
    const uint32_t samples = (duration_ms + interval_ms - 1u) / interval_ms;
    if(samples == 0u || samples > 60000u) return false;
    bool level = false;
    if(!poison_gpio_read(pin, &level)) return false;
    result->level = level;
    result->elapsed_ms = 0u;
    result->samples = 0u;
    for(uint32_t i = 0; i < samples; i++) {
        furi_delay_ms(interval_ms);
        if(!poison_gpio_read(pin, &level)) return false;
        result->level = level;
        result->elapsed_ms += interval_ms;
        result->samples++;
    }
    return true;
}

void poison_gpio_release(PoisonGpioPin pin) {
    const GpioPin* hal_pin = poison_gpio_hal_pin(pin);
    if(hal_pin) furi_hal_gpio_init(hal_pin, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
}
