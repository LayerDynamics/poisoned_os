#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PoisonGpioPinPc0,
    PoisonGpioPinPc1,
    PoisonGpioPinPc3,
    PoisonGpioPinPb2,
    PoisonGpioPinPb3,
    PoisonGpioPinPa4,
    PoisonGpioPinPa6,
    PoisonGpioPinPa7,
} PoisonGpioPin;

typedef enum {
    PoisonGpioInput,
    PoisonGpioOutputPushPull,
    PoisonGpioOutputOpenDrain,
} PoisonGpioMode;

typedef enum {
    PoisonGpioPullNone,
    PoisonGpioPullUp,
    PoisonGpioPullDown,
} PoisonGpioPull;

typedef struct {
    bool level;
    uint32_t elapsed_ms;
    size_t samples;
} PoisonGpioSample;

#ifdef __cplusplus
extern "C" {
#endif

bool poison_gpio_pin_parse(const char* name, PoisonGpioPin* pin);
const char* poison_gpio_pin_name(PoisonGpioPin pin);
bool poison_gpio_configure(PoisonGpioPin pin, PoisonGpioMode mode, PoisonGpioPull pull);
bool poison_gpio_write(PoisonGpioPin pin, bool level);
bool poison_gpio_read(PoisonGpioPin pin, bool* level);
bool poison_gpio_sample(
    PoisonGpioPin pin,
    uint32_t interval_ms,
    uint32_t duration_ms,
    PoisonGpioSample* result);
void poison_gpio_release(PoisonGpioPin pin);

#ifdef __cplusplus
}
#endif
