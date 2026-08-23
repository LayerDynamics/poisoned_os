#pragma once

#include <stdbool.h>
#include <stdint.h>

struct mjs;

typedef enum {
    JsLimitReasonNone,
    JsLimitReasonFuel,
    JsLimitReasonWallTime,
    JsLimitReasonHeap,
    JsLimitReasonSource,
    JsLimitReasonModule,
    JsLimitReasonParser,
    JsLimitReasonStack,
    JsLimitReasonCallback,
    JsLimitReasonTimer,
    JsLimitReasonHandle,
    JsLimitReasonLog,
    JsLimitReasonArtifact,
} JsLimitReason;

typedef struct {
    uint32_t heap_bytes;
    uint32_t source_bytes;
    uint16_t modules;
    uint16_t parser_depth;
    uint16_t stack_depth;
    uint32_t fuel_limit;
    uint16_t callbacks;
    uint16_t timers;
    uint16_t open_handles;
    uint32_t logs;
    uint16_t artifacts;
    uint32_t wall_time_ms;
} JsLimitsConfig;

typedef struct {
    uint32_t heap_bytes;
    uint32_t source_bytes;
    uint16_t modules;
    uint16_t parser_depth;
    uint16_t stack_depth;
    uint32_t fuel;
    uint16_t callbacks;
    uint16_t timers;
    uint16_t open_handles;
    uint32_t logs;
    uint16_t artifacts;
} JsLimitsUsage;

typedef struct {
    JsLimitsConfig config;
    JsLimitsUsage usage;
    uint32_t start_tick;
    JsLimitReason reason;
} JsLimits;

void js_limits_init(JsLimits* limits, const JsLimitsConfig* config, uint32_t start_tick);
bool js_limits_poll(JsLimits* limits, uint32_t now_tick, uint32_t tick_frequency);
bool js_limits_account(JsLimits* limits, const JsLimitsUsage* increment);
bool js_limits_triggered(const JsLimits* limits);
JsLimitReason js_limits_reason(const JsLimits* limits);
const char* js_limits_reason_text(JsLimitReason reason);
