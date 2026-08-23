#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESP32_MARAUDER_ARGUMENT_MAX (128u)
#define ESP32_MARAUDER_COMMAND_MAX  (192u)

typedef enum {
    Esp32MarauderCapabilityObserve,
    Esp32MarauderCapabilityControl,
    Esp32MarauderCapabilityActive,
    Esp32MarauderCapabilityAdmin,
} Esp32MarauderCapability;

typedef struct {
    const char* id;
    const char* label;
    const char* command_template;
    Esp32MarauderCapability capability;
    bool argument_required;
    bool produces_capture;
} Esp32MarauderCommandDescriptor;

void esp32_marauder_driver_on_system_start(void* context);

size_t esp32_marauder_command_count(void);
const Esp32MarauderCommandDescriptor* esp32_marauder_command_at(size_t index);
const Esp32MarauderCommandDescriptor* esp32_marauder_command_find(const char* id);
bool esp32_marauder_command_format(
    const char* id,
    const char* argument,
    char* output,
    size_t output_capacity);

#ifdef __cplusplus
}
#endif
