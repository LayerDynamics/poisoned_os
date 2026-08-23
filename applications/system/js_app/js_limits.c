#include "js_limits.h"

#include <limits.h>

static bool exceeds32(uint32_t current, uint32_t increment, uint32_t maximum) {
    return increment > maximum || current > maximum - increment;
}

static bool exceeds16(uint16_t current, uint16_t increment, uint16_t maximum) {
    return increment > maximum || current > maximum - increment;
}

void js_limits_init(JsLimits* limits, const JsLimitsConfig* config, uint32_t start_tick) {
    if(!limits) return;
    limits->config = config ? *config : (JsLimitsConfig){0};
    limits->usage = (JsLimitsUsage){0};
    limits->start_tick = start_tick;
    limits->reason = JsLimitReasonNone;
}

bool js_limits_poll(JsLimits* limits, uint32_t now_tick, uint32_t tick_frequency) {
    if(!limits || limits->reason != JsLimitReasonNone) return true;
    const JsLimitsUsage fuel = {.fuel = limits->usage.fuel == UINT32_MAX ? 0u : 1u};
    if(!js_limits_account(limits, &fuel)) return true;
    if(limits->config.fuel_limit != 0u && limits->usage.fuel > limits->config.fuel_limit) {
        limits->reason = JsLimitReasonFuel;
        return true;
    }
    if(limits->config.wall_time_ms != 0u && tick_frequency != 0u) {
        const uint32_t elapsed_ticks = now_tick - limits->start_tick;
        const uint64_t elapsed_ms = ((uint64_t)elapsed_ticks * 1000u) / tick_frequency;
        if(elapsed_ms > limits->config.wall_time_ms) {
            limits->reason = JsLimitReasonWallTime;
            return true;
        }
    }
    return false;
}

bool js_limits_account(JsLimits* limits, const JsLimitsUsage* increment) {
    if(!limits || !increment || limits->reason != JsLimitReasonNone) return false;
    JsLimitReason reason = JsLimitReasonNone;
    if(limits->config.heap_bytes &&
       exceeds32(limits->usage.heap_bytes, increment->heap_bytes, limits->config.heap_bytes))
        reason = JsLimitReasonHeap;
    else if(
        limits->config.source_bytes &&
        exceeds32(limits->usage.source_bytes, increment->source_bytes, limits->config.source_bytes))
        reason = JsLimitReasonSource;
    else if(
        limits->config.modules &&
        exceeds16(limits->usage.modules, increment->modules, limits->config.modules))
        reason = JsLimitReasonModule;
    else if(
        limits->config.parser_depth &&
        exceeds16(limits->usage.parser_depth, increment->parser_depth, limits->config.parser_depth))
        reason = JsLimitReasonParser;
    else if(
        limits->config.stack_depth &&
        exceeds16(limits->usage.stack_depth, increment->stack_depth, limits->config.stack_depth))
        reason = JsLimitReasonStack;
    else if(
        limits->config.fuel_limit &&
        exceeds32(limits->usage.fuel, increment->fuel, limits->config.fuel_limit))
        reason = JsLimitReasonFuel;
    else if(
        limits->config.callbacks &&
        exceeds16(limits->usage.callbacks, increment->callbacks, limits->config.callbacks))
        reason = JsLimitReasonCallback;
    else if(
        limits->config.timers &&
        exceeds16(limits->usage.timers, increment->timers, limits->config.timers))
        reason = JsLimitReasonTimer;
    else if(
        limits->config.open_handles &&
        exceeds16(limits->usage.open_handles, increment->open_handles, limits->config.open_handles))
        reason = JsLimitReasonHandle;
    else if(limits->config.logs && exceeds32(limits->usage.logs, increment->logs, limits->config.logs))
        reason = JsLimitReasonLog;
    else if(
        limits->config.artifacts &&
        exceeds16(limits->usage.artifacts, increment->artifacts, limits->config.artifacts))
        reason = JsLimitReasonArtifact;
    if(reason != JsLimitReasonNone) {
        limits->reason = reason;
        return false;
    }
    limits->usage.heap_bytes += increment->heap_bytes;
    limits->usage.source_bytes += increment->source_bytes;
    limits->usage.modules += increment->modules;
    limits->usage.parser_depth += increment->parser_depth;
    limits->usage.stack_depth += increment->stack_depth;
    limits->usage.fuel += increment->fuel;
    limits->usage.callbacks += increment->callbacks;
    limits->usage.timers += increment->timers;
    limits->usage.open_handles += increment->open_handles;
    limits->usage.logs += increment->logs;
    limits->usage.artifacts += increment->artifacts;
    return true;
}

bool js_limits_triggered(const JsLimits* limits) {
    return limits && limits->reason != JsLimitReasonNone;
}

JsLimitReason js_limits_reason(const JsLimits* limits) {
    return limits ? limits->reason : JsLimitReasonNone;
}

const char* js_limits_reason_text(JsLimitReason reason) {
    switch(reason) {
    case JsLimitReasonFuel:
        return "javascript fuel limit exceeded";
    case JsLimitReasonWallTime:
        return "javascript wall-time limit exceeded";
    case JsLimitReasonHeap:
        return "javascript heap limit exceeded";
    case JsLimitReasonSource:
        return "javascript source limit exceeded";
    case JsLimitReasonModule:
        return "javascript module limit exceeded";
    case JsLimitReasonParser:
        return "javascript parser limit exceeded";
    case JsLimitReasonStack:
        return "javascript stack limit exceeded";
    case JsLimitReasonCallback:
        return "javascript callback limit exceeded";
    case JsLimitReasonTimer:
        return "javascript timer limit exceeded";
    case JsLimitReasonHandle:
        return "javascript handle limit exceeded";
    case JsLimitReasonLog:
        return "javascript log limit exceeded";
    case JsLimitReasonArtifact:
        return "javascript artifact limit exceeded";
    default:
        return "javascript limit not exceeded";
    }
}
