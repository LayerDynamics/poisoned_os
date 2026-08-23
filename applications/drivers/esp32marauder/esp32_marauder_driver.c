#include "esp32_marauder_driver.h"

#include <furi.h>
#include <toolbox/compress.h>

#include <stdio.h>
#include <string.h>

#include "esp32_marauder_registry.inc"

static Esp32MarauderCommandDescriptor* esp32_marauder_commands;
static uint8_t* esp32_marauder_registry_decoded;
static bool esp32_marauder_registry_init_attempted;
static bool esp32_marauder_registry_initialized;

static bool esp32_marauder_registry_parse_string(
    uint8_t** cursor,
    const uint8_t* end,
    const char** output) {
    if(!cursor || !*cursor || !output || *cursor >= end) return false;
    const size_t length = *(*cursor)++;
    if(length == 0u || (size_t)(end - *cursor) < length + 1u || (*cursor)[length] != '\0')
        return false;
    *output = (const char*)*cursor;
    *cursor += length + 1u;
    return true;
}

static bool esp32_marauder_registry_init(void) {
    if(esp32_marauder_registry_init_attempted) return esp32_marauder_registry_initialized;
    esp32_marauder_registry_init_attempted = true;

    uint8_t* decoded = malloc(ESP32_MARAUDER_REGISTRY_DECODED_SIZE);
    Esp32MarauderCommandDescriptor* commands =
        malloc(sizeof(Esp32MarauderCommandDescriptor) * ESP32_MARAUDER_REGISTRY_COUNT);
    if(!decoded || !commands) {
        free(decoded);
        free(commands);
        return false;
    }

    Compress* compress =
        compress_alloc(CompressTypeHeatshrink, &compress_config_heatshrink_default);
    size_t decoded_size = 0u;
    const bool decoded_ok = compress_decode(
        compress,
        (uint8_t*)esp32_marauder_registry_encoded,
        sizeof(esp32_marauder_registry_encoded),
        decoded,
        ESP32_MARAUDER_REGISTRY_DECODED_SIZE,
        &decoded_size);
    compress_free(compress);
    if(!decoded_ok || decoded_size != ESP32_MARAUDER_REGISTRY_DECODED_SIZE ||
       memcmp(decoded, "PMR1", 4u) != 0) {
        free(decoded);
        free(commands);
        return false;
    }

    const size_t command_count = (size_t)decoded[4] | ((size_t)decoded[5] << 8u);
    if(command_count != ESP32_MARAUDER_REGISTRY_COUNT) {
        free(decoded);
        free(commands);
        return false;
    }

    uint8_t* cursor = decoded + 6u;
    const uint8_t* end = decoded + decoded_size;
    bool valid = true;
    for(size_t index = 0u; index < command_count && valid; ++index) {
        if((size_t)(end - cursor) < 2u) {
            valid = false;
            break;
        }
        const uint8_t capability = *cursor++;
        const uint8_t flags = *cursor++;
        if(capability > Esp32MarauderCapabilityAdmin || (flags & ~0x03u) != 0u) {
            valid = false;
            break;
        }
        commands[index].capability = (Esp32MarauderCapability)capability;
        commands[index].argument_required = (flags & 0x01u) != 0u;
        commands[index].produces_capture = (flags & 0x02u) != 0u;
        valid =
            esp32_marauder_registry_parse_string(&cursor, end, &commands[index].id) &&
            esp32_marauder_registry_parse_string(&cursor, end, &commands[index].label) &&
            esp32_marauder_registry_parse_string(&cursor, end, &commands[index].command_template);
    }
    if(!valid || cursor != end) {
        free(decoded);
        free(commands);
        return false;
    }

    esp32_marauder_registry_decoded = decoded;
    esp32_marauder_commands = commands;
    esp32_marauder_registry_initialized = true;
    return true;
}

void esp32_marauder_driver_on_system_start(void* context) {
    UNUSED(context);
    furi_check(esp32_marauder_registry_init());
}

void esp32_marauder_driver_start(void) {
    esp32_marauder_driver_on_system_start(NULL);
}

size_t esp32_marauder_command_count(void) {
    return esp32_marauder_registry_init() ? ESP32_MARAUDER_REGISTRY_COUNT : 0u;
}

const Esp32MarauderCommandDescriptor* esp32_marauder_command_at(size_t index) {
    return esp32_marauder_registry_init() && index < ESP32_MARAUDER_REGISTRY_COUNT ?
               &esp32_marauder_commands[index] :
               NULL;
}

const Esp32MarauderCommandDescriptor* esp32_marauder_command_find(const char* id) {
    if(!id || id[0] == '\0') return NULL;
    if(!esp32_marauder_registry_init()) return NULL;
    for(size_t index = 0; index < ESP32_MARAUDER_REGISTRY_COUNT; ++index) {
        if(strcmp(id, esp32_marauder_commands[index].id) == 0) {
            return &esp32_marauder_commands[index];
        }
    }
    return NULL;
}

static bool esp32_marauder_argument_valid(const char* argument) {
    if(!argument) return false;
    const size_t length = strnlen(argument, ESP32_MARAUDER_ARGUMENT_MAX + 1u);
    if(length == 0u || length > ESP32_MARAUDER_ARGUMENT_MAX) return false;
    for(size_t index = 0; index < length; ++index) {
        const uint8_t value = (uint8_t)argument[index];
        if(value < 0x20u || value == 0x7fu) return false;
    }
    return true;
}

bool esp32_marauder_command_format(
    const char* id,
    const char* argument,
    char* output,
    size_t output_capacity) {
    if(!output || output_capacity == 0u) return false;
    const Esp32MarauderCommandDescriptor* descriptor = esp32_marauder_command_find(id);
    if(!descriptor) return false;
    const char* placeholder = strstr(descriptor->command_template, "{arg}");
    if(descriptor->argument_required != (placeholder != NULL)) return false;
    if(descriptor->argument_required && !esp32_marauder_argument_valid(argument)) return false;
    if(!descriptor->argument_required && argument && argument[0] != '\0') return false;

    int written;
    if(placeholder) {
        const size_t prefix = (size_t)(placeholder - descriptor->command_template);
        written = snprintf(
            output,
            output_capacity,
            "%.*s%s%s\n",
            (int)prefix,
            descriptor->command_template,
            argument,
            placeholder + sizeof("{arg}") - 1u);
    } else {
        written = snprintf(output, output_capacity, "%s\n", descriptor->command_template);
    }
    return written > 0 && (size_t)written < output_capacity;
}
