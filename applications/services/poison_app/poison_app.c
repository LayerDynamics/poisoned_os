#include "poison_app.h"

#include <stdio.h>
#include <string.h>

#include <furi.h>

#define RECORD_POISON_APP "poison_app"

typedef struct {
    FuriMutex* mutex;
    PoisonAppRun run;
    PoisonAppCommandCallback command_callback;
    void* command_context;
    PoisonAppEventCallback event_callback;
    void* event_context;
} PoisonAppService;

static PoisonAppService* poison_app_service = NULL;

static bool bounded_text(const char* value, size_t capacity) {
    return value && value[0] != '\0' && strnlen(value, capacity) < capacity;
}

static bool poison_app_digest_is_valid(const char* value) {
    if(!value || strlen(value) != 64u) return false;
    for(size_t index = 0u; index < 64u; index++) {
        if(!((value[index] >= '0' && value[index] <= '9') ||
             (value[index] >= 'a' && value[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

static bool poison_app_artifact_path_is_valid(const char* value) {
    return bounded_text(value, 257u) && strncmp(value, "/ext/", 5u) == 0 &&
           strstr(value, "//") == NULL && strstr(value, "/../") == NULL &&
           strcmp(value + strlen(value) - 3u, "/..") != 0;
}

void poison_app_on_system_start(void) {
    furi_check(!poison_app_service);
    poison_app_service = malloc(sizeof(*poison_app_service));
    furi_check(poison_app_service);
    memset(poison_app_service, 0, sizeof(*poison_app_service));
    poison_app_service->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    furi_record_create(RECORD_POISON_APP, poison_app_service);
}

bool poison_app_run_start(PoisonAppRun* run, const char* app_id, const char* run_id) {
    if(!run || run->active || !bounded_text(app_id, POISON_APP_MAX_TEXT) ||
       !bounded_text(run_id, POISON_APP_MAX_TEXT)) {
        return false;
    }
    memset(run, 0, sizeof(*run));
    snprintf(run->app_id, sizeof(run->app_id), "%s", app_id);
    snprintf(run->run_id, sizeof(run->run_id), "%s", run_id);
    run->active = true;
    return true;
}

bool poison_app_accept_event(PoisonAppRun* run, const PoisonAppEvent* event) {
    if(!run || !event || !run->active || run->cancelled ||
       strcmp(run->app_id, event->app_id) != 0 || strcmp(run->run_id, event->run_id) != 0 ||
       event->sequence != run->next_sequence || event->kind > PoisonAppEventResult) {
        return false;
    }
    run->next_sequence++;
    return true;
}

bool poison_app_cancel(PoisonAppRun* run) {
    if(!run || !run->active || run->cancelled) return false;
    run->cancelled = true;
    run->active = false;
    return true;
}

static bool poison_app_event_is_typed(const PoisonAppEvent* event) {
    if(!event || event->kind > PoisonAppEventResult ||
       !bounded_text(event->event_id, POISON_APP_MAX_TEXT))
        return false;
    switch(event->kind) {
    case PoisonAppEventLog:
        return bounded_text(event->level, 17u) &&
               bounded_text(event->message, POISON_APP_MAX_MESSAGE);
    case PoisonAppEventProgress:
        return event->percent <= 100u && bounded_text(event->label, 129u);
    case PoisonAppEventForm:
        return bounded_text(event->schema_json, 4097u);
    case PoisonAppEventTable:
        return bounded_text(event->schema_json, 4097u) && bounded_text(event->rows_json, 8193u);
    case PoisonAppEventArtifact:
        return bounded_text(event->artifact_name, 129u) &&
               poison_app_artifact_path_is_valid(event->artifact_path) &&
               poison_app_digest_is_valid(event->artifact_sha256);
    case PoisonAppEventResult:
        return bounded_text(event->message, POISON_APP_MAX_MESSAGE);
    default:
        return false;
    }
}

bool poison_app_endpoint_register(
    const char* app_id,
    const char* run_id,
    PoisonAppCommandCallback callback,
    void* context) {
    if(!poison_app_service || !callback || !context) return false;
    furi_check(furi_mutex_acquire(poison_app_service->mutex, FuriWaitForever) == FuriStatusOk);
    bool registered = !poison_app_service->run.active &&
                      poison_app_run_start(&poison_app_service->run, app_id, run_id);
    if(registered) {
        poison_app_service->command_callback = callback;
        poison_app_service->command_context = context;
    }
    furi_check(furi_mutex_release(poison_app_service->mutex) == FuriStatusOk);
    return registered;
}

void poison_app_endpoint_unregister(void* context) {
    if(!poison_app_service || !context) return;
    furi_check(furi_mutex_acquire(poison_app_service->mutex, FuriWaitForever) == FuriStatusOk);
    if(poison_app_service->command_context == context) {
        memset(&poison_app_service->run, 0, sizeof(poison_app_service->run));
        poison_app_service->command_callback = NULL;
        poison_app_service->command_context = NULL;
    }
    furi_check(furi_mutex_release(poison_app_service->mutex) == FuriStatusOk);
}

bool poison_app_dispatch_command(const PoisonAppCommand* command) {
    if(!poison_app_service || !command ||
       command->protocol_version != POISON_APP_PROTOCOL_VERSION ||
       !bounded_text(command->app_id, POISON_APP_MAX_TEXT) ||
       !bounded_text(command->run_id, POISON_APP_MAX_TEXT) ||
       !bounded_text(command->command_id, POISON_APP_MAX_TEXT) || !command->payload_json ||
       strnlen(command->payload_json, 4097u) >= 4097u) {
        return false;
    }
    furi_check(furi_mutex_acquire(poison_app_service->mutex, FuriWaitForever) == FuriStatusOk);
    PoisonAppCommandCallback callback = poison_app_service->command_callback;
    void* context = poison_app_service->command_context;
    const bool matches = poison_app_service->run.active &&
                         strcmp(poison_app_service->run.app_id, command->app_id) == 0 &&
                         strcmp(poison_app_service->run.run_id, command->run_id) == 0;
    furi_check(furi_mutex_release(poison_app_service->mutex) == FuriStatusOk);
    if(!matches || !callback || !callback(command, context)) return false;
    if(command->cancel) {
        furi_check(furi_mutex_acquire(poison_app_service->mutex, FuriWaitForever) == FuriStatusOk);
        const bool cancelled = poison_app_cancel(&poison_app_service->run);
        furi_check(furi_mutex_release(poison_app_service->mutex) == FuriStatusOk);
        return cancelled;
    }
    return true;
}

bool poison_app_publish_event(const PoisonAppEvent* event) {
    if(!poison_app_service || !poison_app_event_is_typed(event)) return false;
    furi_check(furi_mutex_acquire(poison_app_service->mutex, FuriWaitForever) == FuriStatusOk);
    const bool accepted = poison_app_accept_event(&poison_app_service->run, event);
    PoisonAppEventCallback callback = poison_app_service->event_callback;
    void* context = poison_app_service->event_context;
    furi_check(furi_mutex_release(poison_app_service->mutex) == FuriStatusOk);
    if(accepted && callback) callback(event, context);
    return accepted;
}

bool poison_app_event_subscribe(PoisonAppEventCallback callback, void* context) {
    if(!poison_app_service || !callback || !context) return false;
    furi_check(furi_mutex_acquire(poison_app_service->mutex, FuriWaitForever) == FuriStatusOk);
    const bool available = !poison_app_service->event_callback ||
                           poison_app_service->event_context == context;
    if(available) {
        poison_app_service->event_callback = callback;
        poison_app_service->event_context = context;
    }
    furi_check(furi_mutex_release(poison_app_service->mutex) == FuriStatusOk);
    return available;
}

void poison_app_event_unsubscribe(void* context) {
    if(!poison_app_service || !context) return;
    furi_check(furi_mutex_acquire(poison_app_service->mutex, FuriWaitForever) == FuriStatusOk);
    if(poison_app_service->event_context == context) {
        poison_app_service->event_callback = NULL;
        poison_app_service->event_context = NULL;
    }
    furi_check(furi_mutex_release(poison_app_service->mutex) == FuriStatusOk);
}
