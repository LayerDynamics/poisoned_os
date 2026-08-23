#pragma once

#include "poison_app.h"

/* The SDK keeps structured events bounded and sequence-checked before they
 * cross the RPC boundary. Callers must still authorize commands through the
 * active session authorization service. */
static inline bool
    poison_app_sdk_start(PoisonAppRun* run, const char* app_id, const char* run_id) {
    return poison_app_run_start(run, app_id, run_id);
}

static inline bool poison_app_sdk_emit(PoisonAppRun* run, const PoisonAppEvent* event) {
    return poison_app_accept_event(run, event);
}

static inline bool poison_app_sdk_cancel(PoisonAppRun* run) {
    return poison_app_cancel(run);
}

static inline bool poison_app_sdk_register(
    const char* app_id,
    const char* run_id,
    PoisonAppCommandCallback callback,
    void* context) {
    return poison_app_endpoint_register(app_id, run_id, callback, context);
}

static inline void poison_app_sdk_unregister(void* context) {
    poison_app_endpoint_unregister(context);
}

static inline bool poison_app_sdk_publish(const PoisonAppEvent* event) {
    return poison_app_publish_event(event);
}
