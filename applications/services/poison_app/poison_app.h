#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_APP_MAX_TEXT         128u
#define POISON_APP_MAX_MESSAGE      1025u
#define POISON_APP_PROTOCOL_VERSION 1u

typedef enum {
    PoisonAppEventLog,
    PoisonAppEventProgress,
    PoisonAppEventForm,
    PoisonAppEventTable,
    PoisonAppEventArtifact,
    PoisonAppEventResult,
} PoisonAppEventKind;

typedef struct {
    char app_id[POISON_APP_MAX_TEXT];
    char run_id[POISON_APP_MAX_TEXT];
    uint64_t next_sequence;
    bool active;
    bool cancelled;
} PoisonAppRun;

typedef struct {
    char app_id[POISON_APP_MAX_TEXT];
    char run_id[POISON_APP_MAX_TEXT];
    char event_id[POISON_APP_MAX_TEXT];
    uint64_t sequence;
    PoisonAppEventKind kind;
    char message[POISON_APP_MAX_MESSAGE];
    const char* level;
    uint32_t percent;
    const char* label;
    const char* schema_json;
    const char* rows_json;
    const char* artifact_name;
    const char* artifact_path;
    const char* artifact_sha256;
    uint64_t artifact_size;
    bool success;
} PoisonAppEvent;

typedef struct {
    uint32_t protocol_version;
    const char* app_id;
    const char* run_id;
    const char* command_id;
    const char* payload_json;
    bool cancel;
} PoisonAppCommand;

typedef bool (*PoisonAppCommandCallback)(const PoisonAppCommand* command, void* context);
typedef void (*PoisonAppEventCallback)(const PoisonAppEvent* event, void* context);

void poison_app_on_system_start(void);
bool poison_app_run_start(PoisonAppRun* run, const char* app_id, const char* run_id);
bool poison_app_accept_event(PoisonAppRun* run, const PoisonAppEvent* event);
bool poison_app_cancel(PoisonAppRun* run);
bool poison_app_endpoint_register(
    const char* app_id,
    const char* run_id,
    PoisonAppCommandCallback callback,
    void* context);
void poison_app_endpoint_unregister(void* context);
bool poison_app_publish_event(const PoisonAppEvent* event);

#ifdef __cplusplus
}
#endif
