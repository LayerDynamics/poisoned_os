#include "poison_tools.h"
#include "poison_tool_adapters.h"
#include "poison_ble_adapter.h"
#include "poison_gpio_adapter.h"
#include "poison_ibutton_adapter.h"
#include "poison_infrared_adapter.h"
#include "poison_lfrfid_adapter.h"
#include "poison_marauder_adapter.h"
#include "poison_nfc_adapter.h"
#include "poison_serial_adapter.h"
#include "poison_storage_adapter.h"
#include "poison_subghz_adapter.h"
#include "poison_usb_hid_adapter.h"
#include "adapters/poison_tool_nfc.h"
#include "adapters/poison_tool_lfrfid.h"
#include "adapters/poison_tool_ibutton.h"
#include "adapters/poison_tool_infrared.h"
#include "adapters/poison_tool_subghz.h"

#include "../poison_app/poison_app.h"
#include "../poison_evidence/poison_evidence_i.h"
#include "../poison_profiles/poison_profiles_i.h"
#include "poison_tools_i.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <furi.h>
#include <furi_hal_region.h>
#include <applications/drivers/esp32marauder/esp32_marauder_driver.h>

#define POISON_TOOLS_APP_ID          "org.poison.tools"
#define POISON_TOOLS_IDLE_FLAG       (1u << 0)
#define POISON_TOOLS_WAIT_SLICE_MS   50u
#define POISON_TOOLS_STOP_TIMEOUT_MS 6000u

typedef struct {
    FuriMutex* mutex;
    FuriSemaphore* work_available;
    FuriEventFlag* worker_events;
    FuriThread* worker;
    char tool_id[65];
    char run_id[65];
    char case_id[65];
    char pending_command_id[65];
    char pending_payload[4097];
    PoisonRole role;
    uint64_t event_sequence;
    bool active;
    bool cancelled;
    bool work_pending;
    bool worker_busy;
    PoisonInfraredResult* infrared_capture;
    PoisonSubGhzResult* subghz_capture;
} PoisonToolsService;

static PoisonToolsService* poison_tools_service;
static int32_t poison_tools_worker(void* context);

static const PoisonToolDefinition poison_tool_definitions[] = {
    {"nfc.read", "nfc", "nfc.detect", POISON_CAPABILITY_CONTROL | POISON_CAPABILITY_RADIO},
    {"lf-rfid.read",
     "lf-rfid",
     "lf-rfid.detect",
     POISON_CAPABILITY_CONTROL | POISON_CAPABILITY_RADIO},
    {"ibutton.read",
     "ibutton",
     "ibutton.read",
     POISON_CAPABILITY_CONTROL | POISON_CAPABILITY_RADIO},
    {"infrared.receive",
     "infrared",
     "infrared.receive",
     POISON_CAPABILITY_CONTROL | POISON_CAPABILITY_RADIO},
    {"sub-ghz.receive",
     "sub-ghz",
     "sub-ghz.receive",
     POISON_CAPABILITY_CONTROL | POISON_CAPABILITY_RADIO},
    {"gpio.inspect", "gpio", "gpio.read", POISON_CAPABILITY_CONTROL | POISON_CAPABILITY_RADIO},
    {"usb-hid.inspect", "usb-hid", "usb-hid.status", POISON_CAPABILITY_STATUS},
    {"ble-hid.status", "ble-hid", "ble-hid.status", POISON_CAPABILITY_STATUS},
    {"serial.observe",
     "serial",
     "serial.observe",
     POISON_CAPABILITY_CONTROL | POISON_CAPABILITY_RADIO},
    {"storage.inspect", "storage", "storage.inspect", POISON_CAPABILITY_FILES},
    {"marauder.console",
     "serial",
     "marauder.command",
     POISON_CAPABILITY_CONTROL | POISON_CAPABILITY_RADIO},
};

static bool bounded_identifier(const char* value, size_t max_length) {
    if(!value || value[0] == '\0' || strnlen(value, max_length + 1u) > max_length) return false;
    for(const char* cursor = value; *cursor; cursor++) {
        if(!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= '0' && *cursor <= '9') ||
             *cursor == '.' || *cursor == '-' || *cursor == '_'))
            return false;
    }
    return true;
}

void poison_tools_on_system_start(void) {
    furi_check(!poison_tools_service);
    poison_tools_service = malloc(sizeof(*poison_tools_service));
    furi_check(poison_tools_service);
    memset(poison_tools_service, 0, sizeof(*poison_tools_service));
    poison_tools_service->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    poison_tools_service->work_available = furi_semaphore_alloc(1u, 0u);
    poison_tools_service->worker_events = furi_event_flag_alloc();
    poison_tools_service->worker =
        furi_thread_alloc_ex("PoisonTools", 6u * 1024u, poison_tools_worker, poison_tools_service);
    furi_check(
        poison_tools_service->mutex && poison_tools_service->work_available &&
        poison_tools_service->worker_events && poison_tools_service->worker);
    furi_event_flag_set(poison_tools_service->worker_events, POISON_TOOLS_IDLE_FLAG);
    furi_thread_start(poison_tools_service->worker);
}

static bool poison_tools_is_cancelled(void) {
    PoisonToolsService* service = poison_tools_service;
    if(!service) return true;
    furi_check(furi_mutex_acquire(service->mutex, FuriWaitForever) == FuriStatusOk);
    const bool cancelled = !service->active || service->cancelled;
    furi_check(furi_mutex_release(service->mutex) == FuriStatusOk);
    return cancelled;
}

static bool poison_tools_storage_cancelled(void* context) {
    UNUSED(context);
    return poison_tools_is_cancelled();
}

static uint32_t poison_tools_wait_slice(uint32_t remaining_ms) {
    return remaining_ms < POISON_TOOLS_WAIT_SLICE_MS ? remaining_ms : POISON_TOOLS_WAIT_SLICE_MS;
}

bool poison_tool_descriptor_valid(const PoisonToolDescriptor* descriptor) {
    return descriptor && bounded_identifier(descriptor->id, 64u) &&
           bounded_identifier(descriptor->family, 32u) &&
           bounded_identifier(descriptor->capability, 64u) &&
           descriptor->status <= PoisonToolCatalogUnavailable;
}

bool poison_tool_can_execute(
    const PoisonToolDescriptor* descriptor,
    const char* requested_capability) {
    PoisonToolAdapter adapter;
    return poison_tool_descriptor_valid(descriptor) && descriptor->adapter_present &&
           descriptor->status == PoisonToolCatalogVerified && requested_capability &&
           strcmp(descriptor->capability, requested_capability) == 0 &&
           poison_tool_adapter_for_tool(descriptor->id, descriptor->family, &adapter) &&
           poison_tool_adapter_supports_capability(adapter, requested_capability);
}

const PoisonToolDefinition* poison_tool_definition_find(const char* tool_id) {
    if(!tool_id) return NULL;
    for(size_t index = 0u;
        index < sizeof(poison_tool_definitions) / sizeof(poison_tool_definitions[0]);
        index++) {
        if(strcmp(poison_tool_definitions[index].tool_id, tool_id) == 0)
            return &poison_tool_definitions[index];
    }
    return NULL;
}

bool poison_tool_definition_authorized(const PoisonToolDefinition* definition, PoisonRole role) {
    if(!definition || role >= PoisonRoleCount) return false;
    const PoisonCapability granted = poison_policy_role_capabilities(role);
    return (granted & definition->required_capabilities) == definition->required_capabilities;
}

static bool poison_tools_json_object(const char* json) {
    if(!json) return false;
    const size_t length = strnlen(json, 4097u);
    return length >= 2u && length < 4097u && json[0] == '{' && json[length - 1u] == '}';
}

static const char* poison_tools_json_value(const char* json, const char* key) {
    if(!poison_tools_json_object(json) || !key) return NULL;
    char quoted[68];
    if(snprintf(quoted, sizeof(quoted), "\"%s\"", key) <= 0) return NULL;
    const char* found = strstr(json, quoted);
    if(!found) return NULL;
    const char* boundary = found;
    while(boundary > json && (boundary[-1] == ' ' || boundary[-1] == '\t'))
        boundary--;
    if(boundary == json || (boundary[-1] != '{' && boundary[-1] != ',')) return NULL;
    if(strstr(found + strlen(quoted), quoted)) return NULL;
    found += strlen(quoted);
    while(*found == ' ' || *found == '\t')
        found++;
    if(*found++ != ':') return NULL;
    while(*found == ' ' || *found == '\t')
        found++;
    return found;
}

static bool poison_tools_json_value_terminated(const char* cursor) {
    while(*cursor == ' ' || *cursor == '\t')
        cursor++;
    return *cursor == ',' || *cursor == '}';
}

static bool poison_tools_json_uint(
    const char* json,
    const char* key,
    uint32_t minimum,
    uint32_t maximum,
    uint32_t* output) {
    const char* value = poison_tools_json_value(json, key);
    if(!value || *value < '0' || *value > '9' || !output) return false;
    uint64_t parsed = 0u;
    while(*value >= '0' && *value <= '9') {
        parsed = parsed * 10u + (uint32_t)(*value++ - '0');
        if(parsed > maximum) return false;
    }
    if(parsed < minimum || parsed > maximum || !poison_tools_json_value_terminated(value))
        return false;
    *output = (uint32_t)parsed;
    return true;
}

static bool poison_tools_json_bool(const char* json, const char* key, bool* output) {
    const char* value = poison_tools_json_value(json, key);
    if(!value || !output) return false;
    if(strncmp(value, "true", 4u) == 0 && poison_tools_json_value_terminated(value + 4u)) {
        *output = true;
        return true;
    }
    if(strncmp(value, "false", 5u) == 0 && poison_tools_json_value_terminated(value + 5u)) {
        *output = false;
        return true;
    }
    return false;
}

static bool
    poison_tools_json_string(const char* json, const char* key, char* output, size_t capacity) {
    const char* value = poison_tools_json_value(json, key);
    if(!value || *value++ != '"' || !output || capacity < 2u) return false;
    size_t length = 0u;
    while(value[length] && value[length] != '"') {
        const unsigned char byte = (unsigned char)value[length];
        if(byte < 0x20u || byte == '\\' || length + 1u >= capacity) return false;
        length++;
    }
    if(value[length] != '"' || !poison_tools_json_value_terminated(value + length + 1u))
        return false;
    memcpy(output, value, length);
    output[length] = '\0';
    return true;
}

bool poison_tools_json_escape_string(const char* input, char* output, size_t capacity) {
    if(!input || !output || capacity == 0u) return false;
    static const char hex[] = "0123456789abcdef";
    size_t used = 0u;
    while(*input) {
        const uint8_t byte = (uint8_t)*input++;
        const char* escape = NULL;
        if(byte == '"')
            escape = "\\\"";
        else if(byte == '\\')
            escape = "\\\\";
        else if(byte == '\b')
            escape = "\\b";
        else if(byte == '\f')
            escape = "\\f";
        else if(byte == '\n')
            escape = "\\n";
        else if(byte == '\r')
            escape = "\\r";
        else if(byte == '\t')
            escape = "\\t";

        if(escape) {
            if(used + 2u >= capacity) return false;
            output[used++] = escape[0];
            output[used++] = escape[1];
        } else if(byte < 0x20u || byte >= 0x7fu) {
            if(used + 6u >= capacity) return false;
            output[used++] = '\\';
            output[used++] = 'u';
            output[used++] = '0';
            output[used++] = '0';
            output[used++] = hex[byte >> 4u];
            output[used++] = hex[byte & 0x0fu];
        } else {
            if(used + 1u >= capacity) return false;
            output[used++] = (char)byte;
        }
    }
    output[used] = '\0';
    return true;
}

static bool poison_tools_publish_result_with_evidence(
    bool success,
    const char* message,
    const uint8_t* evidence_data,
    size_t evidence_size,
    bool evidence_derived) {
    PoisonToolsService* service = poison_tools_service;
    if(!service || poison_tools_is_cancelled() || !message ||
       strlen(message) >= POISON_APP_MAX_MESSAGE || !evidence_data || evidence_size == 0u)
        return false;
    char evidence_id[65];
    const int evidence_length = snprintf(
        evidence_id, sizeof(evidence_id), "%.40s-%llu", service->run_id, service->event_sequence);
    const uint8_t previous_audit[32] = {0};
    if(evidence_length <= 0 || (size_t)evidence_length >= sizeof(evidence_id) ||
       !poison_evidence_capture_global(
           evidence_id,
           service->case_id,
           evidence_data,
           evidence_size,
           evidence_derived,
           previous_audit)) {
        return false;
    }
    PoisonAppEvent event = {
        .sequence = service->event_sequence,
        .kind = PoisonAppEventResult,
        .success = success,
    };
    strcpy(event.app_id, POISON_TOOLS_APP_ID);
    strcpy(event.run_id, service->run_id);
    strcpy(event.event_id, evidence_id);
    strcpy(event.message, message);
    if(!poison_app_publish_event(&event)) return false;
    service->event_sequence++;
    return true;
}

static bool poison_tools_publish_result(bool success, const char* message) {
    return message && poison_tools_publish_result_with_evidence(
                          success, message, (const uint8_t*)message, strlen(message), true);
}

static bool poison_tools_publish_derived_artifact(
    const char* kind,
    const uint8_t* evidence_data,
    size_t evidence_size) {
    PoisonToolsService* service = poison_tools_service;
    if(!service || poison_tools_is_cancelled() || !kind || !evidence_data || evidence_size == 0u)
        return false;
    char evidence_id[65];
    const int evidence_length = snprintf(
        evidence_id,
        sizeof(evidence_id),
        "%.32s-%llu-derived",
        service->run_id,
        service->event_sequence);
    const uint8_t previous_audit[32] = {0};
    if(evidence_length <= 0 || (size_t)evidence_length >= sizeof(evidence_id) ||
       !poison_evidence_capture_global(
           evidence_id, service->case_id, evidence_data, evidence_size, true, previous_audit)) {
        return false;
    }
    PoisonAppEvent event = {
        .sequence = service->event_sequence,
        .kind = PoisonAppEventArtifact,
        .success = true,
    };
    strcpy(event.app_id, POISON_TOOLS_APP_ID);
    strcpy(event.run_id, service->run_id);
    strcpy(event.event_id, evidence_id);
    if(snprintf(
           event.message,
           sizeof(event.message),
           "{\"kind\":\"%s\",\"redacted\":true,\"bytes\":%lu}",
           kind,
           (unsigned long)evidence_size) <= 0 ||
       !poison_app_publish_event(&event)) {
        return false;
    }
    service->event_sequence++;
    return true;
}

static bool poison_tools_execute_nfc(const char* payload) {
    uint32_t timeout = 0u;
    if(!poison_tools_json_uint(payload, "timeout_ms", 1u, 5000u, &timeout)) return false;
    const PoisonToolNfcRequest request = {
        .operation = PoisonToolNfcOperationDetect,
        .timeout_ms = timeout,
        .maximum_capture_bytes = 0u,
        .exact_confirmation = false,
    };
    char message[POISON_APP_MAX_MESSAGE];
    const PoisonToolNfcRunResult result = poison_tool_nfc_detect(
        &request,
        PoisonToolNfcCapabilityRead,
        poison_tools_storage_cancelled,
        NULL,
        message,
        sizeof(message));
    if(result != PoisonToolNfcRunOk) {
        const char* error = poison_tool_nfc_run_result_name(result);
        if(!error || snprintf(message, sizeof(message), "{\"error\":\"%s\"}", error) <= 0)
            return false;
    }
    return poison_tools_publish_result(result == PoisonToolNfcRunOk, message);
}

static bool poison_tools_execute_lfrfid(const char* payload) {
    uint32_t timeout = 0u;
    if(!poison_tools_json_uint(payload, "timeout_ms", 1u, 5000u, &timeout)) return false;
    const PoisonToolLfRfidRequest request = {
        .operation = PoisonToolLfRfidOperationRead,
        .timeout_ms = timeout,
        .exact_confirmation = false,
    };
    char message[POISON_APP_MAX_MESSAGE];
    const PoisonToolLfRfidRunResult result = poison_tool_lfrfid_read(
        &request,
        PoisonToolLfRfidCapabilityRead,
        poison_tools_storage_cancelled,
        NULL,
        message,
        sizeof(message));
    if(result != PoisonToolLfRfidRunOk) {
        const char* error = poison_tool_lfrfid_run_result_name(result);
        if(!error || snprintf(message, sizeof(message), "{\"error\":\"%s\"}", error) <= 0)
            return false;
    }
    return poison_tools_publish_result(result == PoisonToolLfRfidRunOk, message);
}

static bool poison_tools_execute_ibutton(const char* payload) {
    if(!poison_tools_json_object(payload)) return false;
    uint32_t timeout = 5000u;
    if(strcmp(payload, "{}") != 0 &&
       !poison_tools_json_uint(payload, "timeout_ms", 1u, 5000u, &timeout)) {
        return false;
    }
    const PoisonToolIButtonRequest request = {
        .operation = PoisonToolIButtonOperationRead,
        .timeout_ms = timeout,
        .exact_confirmation = false,
    };
    PoisonIbuttonReadResult evidence;
    char message[POISON_APP_MAX_MESSAGE];
    const PoisonToolIButtonRunResult result = poison_tool_ibutton_read(
        &request,
        PoisonToolIButtonCapabilityRead,
        poison_tools_storage_cancelled,
        NULL,
        &evidence,
        message,
        sizeof(message));
    bool published = false;
    if(result == PoisonToolIButtonRunOk) {
        published = poison_tools_publish_result_with_evidence(
            true, message, evidence.data, evidence.data_size, false);
    } else {
        const char* error = poison_tool_ibutton_run_result_name(result);
        if(error && snprintf(message, sizeof(message), "{\"error\":\"%s\"}", error) > 0)
            published = poison_tools_publish_result(false, message);
    }
    memset(&evidence, 0, sizeof(evidence));
    return published;
}

static void poison_tools_infrared_capture_clear(PoisonToolsService* service) {
    if(!service || !service->infrared_capture) return;
    memset(service->infrared_capture, 0, sizeof(*service->infrared_capture));
    free(service->infrared_capture);
    service->infrared_capture = NULL;
}

static bool
    poison_tools_execute_infrared_receive(PoisonToolsService* service, const char* payload) {
    if(!service) return false;
    uint32_t timeout = 0u;
    uint32_t maximum_timings = 0u;
    if(!poison_tools_json_uint(payload, "timeout_ms", 1u, 5000u, &timeout) ||
       !poison_tools_json_uint(
           payload, "maximum_timings", 1u, MAX_TIMINGS_AMOUNT, &maximum_timings)) {
        return false;
    }
    const PoisonToolInfraredRequest request = {
        .operation = PoisonToolInfraredOperationReceive,
        .timeout_ms = timeout,
        .maximum_timings = maximum_timings,
        .exact_confirmation = false,
    };
    poison_tools_infrared_capture_clear(service);
    PoisonInfraredResult* result = malloc(sizeof(*result));
    uint8_t* evidence = malloc(POISON_TOOL_INFRARED_EVIDENCE_MAX);
    if(!result || !evidence) {
        free(result);
        free(evidence);
        return poison_tools_publish_result(false, "{\"error\":\"infrared-memory\"}");
    }
    size_t evidence_size = 0u;
    char message[POISON_APP_MAX_MESSAGE];
    const PoisonToolInfraredRunResult run_result = poison_tool_infrared_receive(
        &request,
        PoisonToolInfraredCapabilityReceive,
        poison_tools_storage_cancelled,
        NULL,
        result,
        evidence,
        POISON_TOOL_INFRARED_EVIDENCE_MAX,
        &evidence_size,
        message,
        sizeof(message));
    bool published = false;
    if(run_result == PoisonToolInfraredRunOk) {
        published = poison_tools_publish_result_with_evidence(
            true, message, evidence, evidence_size, false);
    } else {
        const char* error = poison_tool_infrared_run_result_name(run_result);
        if(error && snprintf(message, sizeof(message), "{\"error\":\"%s\"}", error) > 0)
            published = poison_tools_publish_result(false, message);
    }
    if(run_result == PoisonToolInfraredRunOk && published) {
        service->infrared_capture = result;
        result = NULL;
    }
    if(result) memset(result, 0, sizeof(*result));
    memset(evidence, 0, POISON_TOOL_INFRARED_EVIDENCE_MAX);
    free(result);
    free(evidence);
    return published;
}

static bool
    poison_tools_execute_infrared_transmit(PoisonToolsService* service, const char* payload) {
    bool exact_confirmation = false;
    if(!service || !service->infrared_capture) {
        return poison_tools_publish_result(false, "{\"error\":\"infrared-no-capture\"}");
    }
    if(!poison_tools_json_bool(payload, "exact_confirmation", &exact_confirmation))
        return poison_tools_publish_result(false, "{\"error\":\"infrared-invalid-request\"}");
    const PoisonCapability granted = poison_policy_role_capabilities(service->role);
    if((granted & POISON_CAPABILITY_DESTRUCTIVE) == 0u) {
        return poison_tools_publish_result(
            false, "{\"error\":\"infrared-transmit-not-authorized\"}");
    }
    const PoisonToolInfraredRequest request = {
        .operation = PoisonToolInfraredOperationTransmit,
        .timeout_ms = 5000u,
        .maximum_timings = MAX_TIMINGS_AMOUNT,
        .exact_confirmation = exact_confirmation,
    };
    uint8_t* evidence = malloc(POISON_TOOL_INFRARED_EVIDENCE_MAX);
    if(!evidence) return poison_tools_publish_result(false, "{\"error\":\"infrared-memory\"}");
    size_t evidence_size = 0u;
    char message[POISON_APP_MAX_MESSAGE];
    const PoisonToolInfraredRunResult run_result = poison_tool_infrared_transmit(
        &request,
        PoisonToolInfraredCapabilityTransmit,
        poison_tools_storage_cancelled,
        NULL,
        service->infrared_capture,
        evidence,
        POISON_TOOL_INFRARED_EVIDENCE_MAX,
        &evidence_size,
        message,
        sizeof(message));
    bool published = false;
    if(run_result == PoisonToolInfraredRunOk) {
        published = poison_tools_publish_result_with_evidence(
            true, message, evidence, evidence_size, false);
    } else {
        const char* error = poison_tool_infrared_run_result_name(run_result);
        if(error && snprintf(message, sizeof(message), "{\"error\":\"%s\"}", error) > 0)
            published = poison_tools_publish_result(false, message);
    }
    memset(evidence, 0, POISON_TOOL_INFRARED_EVIDENCE_MAX);
    free(evidence);
    return published;
}

static void poison_tools_subghz_capture_clear(PoisonToolsService* service) {
    if(!service || !service->subghz_capture) return;
    memset(service->subghz_capture, 0, sizeof(*service->subghz_capture));
    free(service->subghz_capture);
    service->subghz_capture = NULL;
}

static bool poison_tools_profile_enables_tool(const PoisonProfile* profile, const char* tool_id) {
    if(!profile || !tool_id) return false;
    if(profile->enabled_tools_count == 0u) return true;
    for(size_t index = 0u; index < profile->enabled_tools_count; ++index) {
        if(strcmp(profile->enabled_tools[index], tool_id) == 0) return true;
    }
    return false;
}

static bool poison_tools_subghz_policy_snapshot(
    const PoisonToolSubGhzRequest* request,
    PoisonToolSubGhzPolicySnapshot* snapshot,
    void* context) {
    PoisonToolsService* service = context;
    if(!request || !snapshot || !service) return false;
    PoisonProfile* active = malloc(sizeof(*active));
    if(!active) return false;
    if(!poison_profiles_copy_global(active, NULL, NULL)) {
        memset(active, 0, sizeof(*active));
        free(active);
        return false;
    }
    const char* region_name = furi_hal_region_get_name();
    snapshot->region_provisioned = furi_hal_region_is_provisioned();
    snapshot->region_frequency_allowed =
        furi_hal_region_is_frequency_allowed(request->frequency_hz);
    snapshot->profile_region_matches =
        strcmp(active->radio_region, "device") == 0 ||
        (region_name && strcmp(active->radio_region, region_name) == 0);
    snapshot->tool_enabled = poison_tools_profile_enables_tool(active, "sub-ghz.receive");
    snapshot->classroom_restricted = active->classroom_restricted;
    snapshot->classroom_instructor = strcmp(active->classroom_policy, "instructor") == 0;
    snapshot->role_capabilities = poison_policy_role_capabilities(service->role);
    snapshot->profile_capabilities = (PoisonCapability)active->capability_mask;
    memset(active, 0, sizeof(*active));
    free(active);
    return true;
}

static bool poison_tools_execute_subghz_receive(
    PoisonToolsService* service,
    const char* payload,
    PoisonToolSubGhzOperation operation) {
    if(!service || (operation != PoisonToolSubGhzOperationReceive &&
                    operation != PoisonToolSubGhzOperationAnalyze)) {
        return false;
    }
    uint32_t frequency = 0u;
    uint32_t timeout = 0u;
    uint32_t maximum_timings = 0u;
    if(!poison_tools_json_uint(payload, "frequency_hz", 1u, 1000000000u, &frequency) ||
       !poison_tools_json_uint(payload, "timeout_ms", 1u, 60000u, &timeout) ||
       !poison_tools_json_uint(
           payload, "maximum_timings", 1u, POISON_SUBGHZ_RAW_TIMINGS_MAX, &maximum_timings)) {
        return false;
    }
    poison_tools_subghz_capture_clear(service);
    const PoisonToolSubGhzRequest request = {
        .operation = operation,
        .frequency_hz = frequency,
        .timeout_ms = timeout,
        .maximum_timings = maximum_timings,
        .exact_confirmation = false,
    };
    PoisonSubGhzResult* result = malloc(sizeof(*result));
    uint8_t* evidence = malloc(POISON_TOOL_SUBGHZ_EVIDENCE_MAX);
    if(!result || !evidence) {
        free(result);
        free(evidence);
        return poison_tools_publish_result(false, "{\"error\":\"sub-ghz-memory\"}");
    }
    size_t evidence_size = 0u;
    char message[POISON_APP_MAX_MESSAGE];
    const uint32_t capability = operation == PoisonToolSubGhzOperationAnalyze ?
                                    PoisonToolSubGhzCapabilityAnalyze :
                                    PoisonToolSubGhzCapabilityReceive;
    const PoisonToolSubGhzRunResult run_result = poison_tool_subghz_receive(
        &request,
        capability,
        poison_tools_subghz_policy_snapshot,
        service,
        poison_tools_storage_cancelled,
        NULL,
        result,
        evidence,
        POISON_TOOL_SUBGHZ_EVIDENCE_MAX,
        &evidence_size,
        message,
        sizeof(message));
    bool published = false;
    if(run_result == PoisonToolSubGhzRunOk) {
        published = poison_tools_publish_result_with_evidence(
            true, message, evidence, evidence_size, false);
        if(published && result->decoded_valid) {
            published = poison_tools_publish_derived_artifact(
                "sub-ghz.decoded", (const uint8_t*)result->decoded, strlen(result->decoded));
        }
    } else {
        const char* error = poison_tool_subghz_run_result_name(run_result);
        if(error && snprintf(message, sizeof(message), "{\"error\":\"%s\"}", error) > 0)
            published = poison_tools_publish_result(false, message);
    }
    if(run_result == PoisonToolSubGhzRunOk && published) {
        service->subghz_capture = result;
        result = NULL;
    }
    if(result) memset(result, 0, sizeof(*result));
    memset(evidence, 0, POISON_TOOL_SUBGHZ_EVIDENCE_MAX);
    free(result);
    free(evidence);
    return published;
}

static bool
    poison_tools_execute_subghz_transmit(PoisonToolsService* service, const char* payload) {
    if(!service || !service->subghz_capture)
        return poison_tools_publish_result(false, "{\"error\":\"sub-ghz-no-capture\"}");
    bool exact_confirmation = false;
    if(!poison_tools_json_bool(payload, "exact_confirmation", &exact_confirmation))
        return poison_tools_publish_result(false, "{\"error\":\"sub-ghz-invalid-request\"}");
    const PoisonToolSubGhzRequest request = {
        .operation = PoisonToolSubGhzOperationTransmit,
        .frequency_hz = service->subghz_capture->frequency,
        .timeout_ms = 5000u,
        .maximum_timings = service->subghz_capture->raw_count,
        .exact_confirmation = exact_confirmation,
    };
    uint8_t* evidence = malloc(POISON_TOOL_SUBGHZ_EVIDENCE_MAX);
    if(!evidence) return poison_tools_publish_result(false, "{\"error\":\"sub-ghz-memory\"}");
    size_t evidence_size = 0u;
    char message[POISON_APP_MAX_MESSAGE];
    const PoisonToolSubGhzRunResult run_result = poison_tool_subghz_transmit(
        &request,
        PoisonToolSubGhzCapabilityTransmit,
        poison_tools_subghz_policy_snapshot,
        service,
        poison_tools_storage_cancelled,
        NULL,
        service->subghz_capture,
        evidence,
        POISON_TOOL_SUBGHZ_EVIDENCE_MAX,
        &evidence_size,
        message,
        sizeof(message));
    bool published = false;
    if(run_result == PoisonToolSubGhzRunOk) {
        published = poison_tools_publish_result_with_evidence(
            true, message, evidence, evidence_size, false);
    } else {
        const char* error = poison_tool_subghz_run_result_name(run_result);
        if(error && snprintf(message, sizeof(message), "{\"error\":\"%s\"}", error) > 0)
            published = poison_tools_publish_result(false, message);
    }
    memset(evidence, 0, POISON_TOOL_SUBGHZ_EVIDENCE_MAX);
    free(evidence);
    return published;
}

static bool poison_tools_execute_gpio(const char* payload) {
    char pin_name[8];
    PoisonGpioPin pin;
    bool level = false;
    const bool read = poison_tools_json_string(payload, "pin", pin_name, sizeof(pin_name)) &&
                      poison_gpio_pin_parse(pin_name, &pin) &&
                      poison_gpio_configure(pin, PoisonGpioInput, PoisonGpioPullNone) &&
                      poison_gpio_read(pin, &level);
    char message[128];
    snprintf(
        message,
        sizeof(message),
        read ? "{\"pin\":\"%s\",\"level\":%s}" : "{\"error\":\"gpio-read-failed\"}",
        read ? poison_gpio_pin_name(pin) : "",
        read && level ? "true" : "false");
    if(read) poison_gpio_release(pin);
    return poison_tools_publish_result(read, message);
}

static bool poison_tools_execute_serial(const char* payload) {
    char port[16];
    uint32_t baud = 0u;
    uint32_t timeout = 0u;
    uint32_t capacity = 0u;
    if(!poison_tools_json_string(payload, "port", port, sizeof(port)) ||
       !poison_tools_json_uint(payload, "baudrate", 1200u, 2000000u, &baud) ||
       !poison_tools_json_uint(payload, "timeout_ms", 0u, 5000u, &timeout) ||
       !poison_tools_json_uint(payload, "capacity", 1u, POISON_STORAGE_READ_MAX, &capacity)) {
        return false;
    }
    const FuriHalSerialId serial_id = strcmp(port, "usart") == 0  ? FuriHalSerialIdUsart :
                                      strcmp(port, "lpuart") == 0 ? FuriHalSerialIdLpuart :
                                                                    FuriHalSerialIdMax;
    PoisonSerialSession* session = poison_serial_session_alloc(
        serial_id, baud, FuriHalSerialDataBits8, FuriHalSerialParityNone, FuriHalSerialStopBits1);
    uint8_t data[POISON_STORAGE_READ_MAX];
    PoisonSerialReceiveResult result;
    bool observed = false;
    uint32_t remaining = timeout;
    const bool started = session && poison_serial_session_start(session);
    while(started && !poison_tools_is_cancelled()) {
        const uint32_t slice = poison_tools_wait_slice(remaining);
        observed = poison_serial_session_receive(session, data, capacity, slice, &result);
        if(!observed || result.bytes_received != 0u || remaining == 0u) break;
        remaining -= slice;
    }
    char message[POISON_APP_MAX_MESSAGE];
    if(observed) {
        const size_t encoded = result.bytes_received > 400u ? 400u : result.bytes_received;
        size_t used = (size_t)snprintf(
            message,
            sizeof(message),
            "{\"bytes_received\":%u,\"bytes_dropped\":%lu,\"truncated\":%s,\"data\":\"",
            (unsigned int)result.bytes_received,
            (unsigned long)result.bytes_dropped,
            result.truncated || encoded < result.bytes_received ? "true" : "false");
        for(size_t index = 0u; index < encoded && used + 2u < sizeof(message); index++)
            used += (size_t)snprintf(message + used, sizeof(message) - used, "%02x", data[index]);
        snprintf(message + used, sizeof(message) - used, "\"}");
    } else {
        strcpy(message, "{\"error\":\"serial-observe-failed\"}");
    }
    poison_serial_session_free(session);
    return poison_tools_publish_result(observed, message);
}

static bool poison_tools_execute_storage(const PoisonAppCommand* command) {
    char path[257];
    if(!poison_tools_json_string(command->payload_json, "path", path, sizeof(path))) return false;
    char operation[16];
    if(!poison_tools_json_string(command->payload_json, "operation", operation, sizeof(operation)))
        return false;
    if(strcmp(operation, "sha256") == 0) {
        char digest[65];
        const bool hashed = poison_storage_sha256_cancellable(
            path, poison_tools_service->role, digest, poison_tools_storage_cancelled, NULL);
        char message[384];
        snprintf(
            message,
            sizeof(message),
            hashed ? "{\"path\":\"%s\",\"sha256\":\"%s\"}" : "{\"error\":\"storage-hash-failed\"}",
            hashed ? path : "",
            hashed ? digest : "");
        return poison_tools_publish_result(hashed, message);
    }
    if(strcmp(operation, "read") != 0) return false;
    uint32_t offset = 0u;
    uint32_t capacity = 0u;
    if(!poison_tools_json_uint(command->payload_json, "offset", 0u, UINT32_MAX, &offset) ||
       !poison_tools_json_uint(
           command->payload_json, "capacity", 1u, POISON_STORAGE_READ_MAX, &capacity)) {
        return false;
    }
    uint8_t data[POISON_STORAGE_READ_MAX];
    PoisonStorageReadResult result;
    const bool read =
        poison_storage_read(path, poison_tools_service->role, offset, data, capacity, &result);
    char message[POISON_APP_MAX_MESSAGE];
    if(read) {
        const size_t encoded = result.bytes_read > 350u ? 350u : result.bytes_read;
        size_t used = (size_t)snprintf(
            message,
            sizeof(message),
            "{\"path\":\"%s\",\"file_size\":%llu,\"bytes_read\":%u,\"end_of_file\":%s,\"truncated\":%s,\"data\":\"",
            path,
            result.file_size,
            (unsigned int)result.bytes_read,
            result.end_of_file ? "true" : "false",
            encoded < result.bytes_read ? "true" : "false");
        for(size_t index = 0u; index < encoded && used + 2u < sizeof(message); index++)
            used += (size_t)snprintf(message + used, sizeof(message) - used, "%02x", data[index]);
        snprintf(message + used, sizeof(message) - used, "\"}");
    } else {
        strcpy(message, "{\"error\":\"storage-read-failed\"}");
    }
    return poison_tools_publish_result(read, message);
}

static bool poison_tools_execute_marauder(const char* payload) {
    char command_id[65];
    char argument[ESP32_MARAUDER_ARGUMENT_MAX + 1u];
    char port[16];
    uint32_t baudrate = 0u;
    uint32_t timeout = 0u;
    uint32_t capacity = 0u;
    if(!poison_tools_json_string(payload, "command", command_id, sizeof(command_id)) ||
       !poison_tools_json_string(payload, "argument", argument, sizeof(argument)) ||
       !poison_tools_json_string(payload, "port", port, sizeof(port)) ||
       !poison_tools_json_uint(payload, "baudrate", 1200u, 2000000u, &baudrate) ||
       !poison_tools_json_uint(payload, "timeout_ms", 0u, 5000u, &timeout) ||
       !poison_tools_json_uint(payload, "capacity", 1u, 1024u, &capacity)) {
        return false;
    }
    const Esp32MarauderCommandDescriptor* descriptor = esp32_marauder_command_find(command_id);
    if(!descriptor || (descriptor->argument_required && argument[0] == '\0') ||
       (!descriptor->argument_required && argument[0] != '\0')) {
        return false;
    }
    const PoisonCapability granted = poison_policy_role_capabilities(poison_tools_service->role);
    if((descriptor->capability == Esp32MarauderCapabilityActive ||
        descriptor->capability == Esp32MarauderCapabilityAdmin) &&
       (granted & POISON_CAPABILITY_DESTRUCTIVE) == 0u) {
        return poison_tools_publish_result(
            false, "{\"error\":\"marauder-command-not-authorized\"}");
    }
    const FuriHalSerialId serial_id = strcmp(port, "usart") == 0  ? FuriHalSerialIdUsart :
                                      strcmp(port, "lpuart") == 0 ? FuriHalSerialIdLpuart :
                                                                    FuriHalSerialIdMax;
    PoisonMarauderSession* session = poison_marauder_session_alloc(serial_id, baudrate);
    uint8_t response[1024];
    const bool sent =
        session && poison_marauder_session_start(session) &&
        poison_marauder_session_send_command(session, command_id, argument[0] ? argument : NULL);
    size_t received = 0u;
    uint32_t remaining = timeout;
    while(sent && !poison_tools_is_cancelled()) {
        const uint32_t slice = poison_tools_wait_slice(remaining);
        received = poison_marauder_session_receive(session, response, capacity, slice);
        if(received != 0u || remaining == 0u) break;
        remaining -= slice;
    }
    const uint32_t dropped = poison_marauder_session_take_dropped(session);
    char message[POISON_APP_MAX_MESSAGE];
    if(sent) {
        const size_t encoded = received > 350u ? 350u : received;
        size_t used = (size_t)snprintf(
            message,
            sizeof(message),
            "{\"command\":\"%s\",\"capability\":%u,\"capture\":%s,\"bytes_received\":%u,\"bytes_dropped\":%lu,\"truncated\":%s,\"data\":\"",
            command_id,
            (unsigned int)descriptor->capability,
            descriptor->produces_capture ? "true" : "false",
            (unsigned int)received,
            (unsigned long)dropped,
            dropped || encoded < received ? "true" : "false");
        for(size_t index = 0u; index < encoded && used + 2u < sizeof(message); index++)
            used +=
                (size_t)snprintf(message + used, sizeof(message) - used, "%02x", response[index]);
        snprintf(message + used, sizeof(message) - used, "\"}");
    } else {
        strcpy(message, "{\"error\":\"marauder-command-failed\"}");
    }
    poison_marauder_session_free(session);
    return poison_tools_publish_result(sent, message);
}

static bool poison_tools_execute_pending(PoisonToolsService* service) {
    PoisonAppCommand command = {
        .protocol_version = POISON_APP_PROTOCOL_VERSION,
        .app_id = POISON_TOOLS_APP_ID,
        .run_id = service->run_id,
        .command_id = service->pending_command_id,
        .payload_json = service->pending_payload,
        .cancel = false,
    };
    if(strcmp(service->tool_id, "nfc.read") == 0)
        return poison_tools_execute_nfc(command.payload_json);
    if(strcmp(service->tool_id, "lf-rfid.read") == 0)
        return poison_tools_execute_lfrfid(command.payload_json);
    if(strcmp(service->tool_id, "ibutton.read") == 0)
        return poison_tools_execute_ibutton(command.payload_json);
    if(strcmp(service->tool_id, "infrared.receive") == 0) {
        if(strcmp(command.command_id, "infrared.transmit") == 0)
            return poison_tools_execute_infrared_transmit(service, command.payload_json);
        return poison_tools_execute_infrared_receive(service, command.payload_json);
    }
    if(strcmp(service->tool_id, "sub-ghz.receive") == 0) {
        if(strcmp(command.command_id, "sub-ghz.transmit") == 0)
            return poison_tools_execute_subghz_transmit(service, command.payload_json);
        return poison_tools_execute_subghz_receive(
            service,
            command.payload_json,
            strcmp(command.command_id, "sub-ghz.analyze") == 0 ? PoisonToolSubGhzOperationAnalyze :
                                                                 PoisonToolSubGhzOperationReceive);
    }
    if(strcmp(service->tool_id, "gpio.inspect") == 0)
        return poison_tools_execute_gpio(command.payload_json);
    if(strcmp(service->tool_id, "usb-hid.inspect") == 0)
        return poison_tools_publish_result(
            true, poison_usb_hid_connected() ? "{\"connected\":true}" : "{\"connected\":false}");
    if(strcmp(service->tool_id, "ble-hid.status") == 0)
        return poison_tools_publish_result(
            true, poison_ble_is_active() ? "{\"active\":true}" : "{\"active\":false}");
    if(strcmp(service->tool_id, "serial.observe") == 0)
        return poison_tools_execute_serial(command.payload_json);
    if(strcmp(service->tool_id, "storage.inspect") == 0)
        return poison_tools_execute_storage(&command);
    if(strcmp(service->tool_id, "marauder.console") == 0)
        return poison_tools_execute_marauder(command.payload_json);
    return false;
}

static int32_t poison_tools_worker(void* context) {
    PoisonToolsService* service = context;
    while(true) {
        furi_check(
            furi_semaphore_acquire(service->work_available, FuriWaitForever) == FuriStatusOk);
        furi_check(furi_mutex_acquire(service->mutex, FuriWaitForever) == FuriStatusOk);
        const bool execute = service->work_pending && service->active && !service->cancelled;
        service->work_pending = false;
        service->worker_busy = execute;
        furi_check(furi_mutex_release(service->mutex) == FuriStatusOk);

        if(execute) poison_tools_execute_pending(service);

        furi_check(furi_mutex_acquire(service->mutex, FuriWaitForever) == FuriStatusOk);
        service->worker_busy = false;
        furi_event_flag_set(service->worker_events, POISON_TOOLS_IDLE_FLAG);
        furi_check(furi_mutex_release(service->mutex) == FuriStatusOk);
    }
    return 0;
}

static bool poison_tools_command(const PoisonAppCommand* command, void* context) {
    PoisonToolsService* service = context;
    if(!service || !command) return false;
    furi_check(furi_mutex_acquire(service->mutex, FuriWaitForever) == FuriStatusOk);
    const PoisonToolDefinition* definition = poison_tool_definition_find(service->tool_id);
    const bool infrared_transmit = definition &&
                                   strcmp(definition->tool_id, "infrared.receive") == 0 &&
                                   strcmp(command->command_id, "infrared.transmit") == 0;
    const bool subghz_extended = definition &&
                                 strcmp(definition->tool_id, "sub-ghz.receive") == 0 &&
                                 (strcmp(command->command_id, "sub-ghz.analyze") == 0 ||
                                  strcmp(command->command_id, "sub-ghz.transmit") == 0);
    bool accepted = service->active && definition &&
                    (strcmp(command->command_id, definition->command_id) == 0 ||
                     infrared_transmit || subghz_extended);
    bool queued = false;
    if(accepted && command->cancel) {
        accepted = !service->cancelled;
        service->cancelled = true;
    } else if(accepted) {
        const size_t payload_length =
            strnlen(command->payload_json, sizeof(service->pending_payload));
        accepted = !service->cancelled && !service->work_pending && !service->worker_busy &&
                   payload_length < sizeof(service->pending_payload);
        if(accepted) {
            strcpy(service->pending_command_id, command->command_id);
            memcpy(service->pending_payload, command->payload_json, payload_length + 1u);
            service->work_pending = true;
            furi_event_flag_clear(service->worker_events, POISON_TOOLS_IDLE_FLAG);
            queued = true;
        }
    }
    furi_check(furi_mutex_release(service->mutex) == FuriStatusOk);
    if(queued) furi_check(furi_semaphore_release(service->work_available) == FuriStatusOk);
    return accepted;
}

bool poison_tools_run_start_for_case(
    const char* tool_id,
    const char* run_id,
    const char* case_id,
    PoisonRole role) {
    if(!poison_tools_service || !bounded_identifier(tool_id, 64u) ||
       !bounded_identifier(run_id, 64u) || !bounded_identifier(case_id, 64u)) {
        return false;
    }
    const PoisonToolDefinition* definition = poison_tool_definition_find(tool_id);
    if(!poison_tool_definition_authorized(definition, role)) return false;
    furi_check(furi_mutex_acquire(poison_tools_service->mutex, FuriWaitForever) == FuriStatusOk);
    bool started = !poison_tools_service->active && !poison_tools_service->work_pending &&
                   !poison_tools_service->worker_busy;
    if(started) {
        memset(poison_tools_service->tool_id, 0, sizeof(poison_tools_service->tool_id));
        memset(poison_tools_service->run_id, 0, sizeof(poison_tools_service->run_id));
        memset(poison_tools_service->case_id, 0, sizeof(poison_tools_service->case_id));
        strcpy(poison_tools_service->tool_id, tool_id);
        strcpy(poison_tools_service->run_id, run_id);
        strcpy(poison_tools_service->case_id, case_id);
        poison_tools_service->role = role;
        poison_tools_service->event_sequence = 0u;
        poison_tools_service->cancelled = false;
        poison_tools_service->pending_command_id[0] = '\0';
        poison_tools_service->pending_payload[0] = '\0';
        poison_tools_service->active = true;
        started = poison_app_endpoint_register(
            POISON_TOOLS_APP_ID, run_id, poison_tools_command, poison_tools_service);
        if(!started) poison_tools_service->active = false;
    }
    furi_check(furi_mutex_release(poison_tools_service->mutex) == FuriStatusOk);
    return started;
}

bool poison_tools_run_start(const char* tool_id, const char* run_id, PoisonRole role) {
    return poison_tools_run_start_for_case(tool_id, run_id, "local", role);
}

bool poison_tools_run_stop(const char* run_id) {
    if(!poison_tools_service || !run_id) return false;
    furi_check(furi_mutex_acquire(poison_tools_service->mutex, FuriWaitForever) == FuriStatusOk);
    bool stopped = poison_tools_service->active &&
                   strcmp(poison_tools_service->run_id, run_id) == 0;
    if(stopped) poison_tools_service->cancelled = true;
    furi_check(furi_mutex_release(poison_tools_service->mutex) == FuriStatusOk);
    if(!stopped) return false;

    const uint32_t flags = furi_event_flag_wait(
        poison_tools_service->worker_events,
        POISON_TOOLS_IDLE_FLAG,
        FuriFlagWaitAny | FuriFlagNoClear,
        POISON_TOOLS_STOP_TIMEOUT_MS);
    if((flags & POISON_TOOLS_IDLE_FLAG) == 0u) return false;

    furi_check(furi_mutex_acquire(poison_tools_service->mutex, FuriWaitForever) == FuriStatusOk);
    stopped = poison_tools_service->active && strcmp(poison_tools_service->run_id, run_id) == 0 &&
              !poison_tools_service->work_pending && !poison_tools_service->worker_busy;
    if(stopped) {
        poison_tools_service->active = false;
        poison_app_endpoint_unregister(poison_tools_service);
        poison_tools_infrared_capture_clear(poison_tools_service);
        poison_tools_subghz_capture_clear(poison_tools_service);
        memset(poison_tools_service->tool_id, 0, sizeof(poison_tools_service->tool_id));
        memset(poison_tools_service->run_id, 0, sizeof(poison_tools_service->run_id));
        memset(poison_tools_service->case_id, 0, sizeof(poison_tools_service->case_id));
        poison_tools_service->pending_command_id[0] = '\0';
        poison_tools_service->pending_payload[0] = '\0';
    }
    furi_check(furi_mutex_release(poison_tools_service->mutex) == FuriStatusOk);
    return stopped;
}

bool poison_tools_run_is_active(const char* run_id) {
    if(!poison_tools_service || !run_id) return false;
    furi_check(furi_mutex_acquire(poison_tools_service->mutex, FuriWaitForever) == FuriStatusOk);
    const bool active = poison_tools_service->active &&
                        strcmp(poison_tools_service->run_id, run_id) == 0;
    furi_check(furi_mutex_release(poison_tools_service->mutex) == FuriStatusOk);
    return active;
}
