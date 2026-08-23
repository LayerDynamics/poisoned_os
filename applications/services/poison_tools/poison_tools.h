#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "../rpc/poison_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PoisonToolCatalogFoundation,
    PoisonToolCatalogVerified,
    PoisonToolCatalogUnavailable,
} PoisonToolCatalogStatus;

typedef struct {
    const char* id;
    const char* family;
    const char* capability;
    PoisonToolCatalogStatus status;
    bool adapter_present;
} PoisonToolDescriptor;

typedef struct {
    const char* tool_id;
    const char* family;
    const char* command_id;
    PoisonCapability required_capabilities;
} PoisonToolDefinition;

void poison_tools_on_system_start(void);
bool poison_tool_descriptor_valid(const PoisonToolDescriptor* descriptor);
bool poison_tool_can_execute(
    const PoisonToolDescriptor* descriptor,
    const char* requested_capability);
const PoisonToolDefinition* poison_tool_definition_find(const char* tool_id);
bool poison_tool_definition_authorized(const PoisonToolDefinition* definition, PoisonRole role);
bool poison_tools_run_start(const char* tool_id, const char* run_id, PoisonRole role);
bool poison_tools_run_stop(const char* run_id);
bool poison_tools_run_is_active(const char* run_id);

#ifdef __cplusplus
}
#endif
