#pragma once

#include "poison_tools.h"
#include "poison_tool_adapters.h"

#include <stddef.h>

bool poison_tools_run_start_for_case(
    const char* tool_id,
    const char* run_id,
    const char* case_id,
    PoisonRole role);

bool poison_tools_json_escape_string(const char* input, char* output, size_t capacity);

bool poison_tool_adapter_for_tool(
    const char* tool_id,
    const char* family,
    PoisonToolAdapter* adapter);
