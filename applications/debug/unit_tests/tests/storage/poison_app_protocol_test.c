#include "../test.h"
#include "../../../../services/poison_app/poison_app.h"
#include "../../../../services/poison_app/poison_app_i.h"
#include "../../../../services/rpc/rpc_poison_app.h"

#include <stdio.h>

MU_TEST(poison_app_accepts_ordered_events_and_rejects_malformed_events) {
    PoisonAppRun run = {0};
    PoisonAppEvent event = {0};
    mu_check(poison_app_run_start(&run, "org.example", "run-1"));
    snprintf(event.app_id, sizeof(event.app_id), "org.example");
    snprintf(event.run_id, sizeof(event.run_id), "run-1");
    event.kind = PoisonAppEventLog;
    snprintf(event.message, sizeof(event.message), "ready");
    mu_check(poison_app_accept_event(&run, &event));
    event.sequence = 2;
    mu_check(!poison_app_accept_event(&run, &event));
    event.sequence = 1;
    mu_check(poison_app_accept_event(&run, &event));
}

MU_TEST(poison_app_cancel_is_terminal) {
    PoisonAppRun run = {0};
    mu_check(poison_app_run_start(&run, "org.example", "run-1"));
    mu_check(poison_app_cancel(&run));
    mu_check(!poison_app_cancel(&run));
}

MU_TEST(poison_app_rpc_enforces_wire_identifier_capacity) {
    char maximum_id[65];
    char oversized_id[66];
    memset(maximum_id, 'a', sizeof(maximum_id) - 1u);
    maximum_id[sizeof(maximum_id) - 1u] = '\0';
    memset(oversized_id, 'b', sizeof(oversized_id) - 1u);
    oversized_id[sizeof(oversized_id) - 1u] = '\0';

    mu_check(rpc_poison_app_command_is_bounded(maximum_id, maximum_id, "{}"));
    mu_check(!rpc_poison_app_command_is_bounded(oversized_id, maximum_id, "{}"));
    mu_check(!rpc_poison_app_command_is_bounded(maximum_id, oversized_id, "{}"));
}

MU_TEST(poison_app_rpc_requires_authenticated_envelope) {
    PoisonSession session = {0};
    PoisonAppRun run = {0};
    PoisonAppEvent event = {0};
    uint8_t key[POISON_SESSION_KEY_BYTES];
    uint8_t tag[POISON_SESSION_AUTH_TAG_BYTES];
    const uint8_t payload[] = {0x01};
    memset(key, 0xA5, sizeof(key));
    mu_check(poison_session_begin_negotiation(&session, 2u) == PoisonSessionResultInvalid);
    poison_session_init(&session);
    mu_check(poison_session_begin_negotiation(&session, 2u) == PoisonSessionResultOk);
    mu_check(poison_session_begin_confirmation(&session, 9u) == PoisonSessionResultOk);
    mu_check(poison_session_set_authentication_key(&session, key) == PoisonSessionResultOk);
    mu_check(poison_session_confirm(&session, true) == PoisonSessionResultOk);
    mu_check(poison_session_activate(&session) == PoisonSessionResultOk);
    mu_check(poison_app_run_start(&run, "org.example", "run-1"));
    snprintf(event.app_id, sizeof(event.app_id), "org.example");
    snprintf(event.run_id, sizeof(event.run_id), "run-1");
    snprintf(event.message, sizeof(event.message), "ready");
    mu_check(
        poison_session_sign_frame(&session, 2u, 0u, 0u, "app", payload, sizeof(payload), tag) ==
        PoisonSessionResultOk);
    mu_check(rpc_poison_app_accept_event_authenticated(
        &session, 2u, 0u, 0u, "app", payload, sizeof(payload), tag, &run, &event));
    mu_check(!rpc_poison_app_accept_event_authenticated(
        &session, 2u, 1u, 0u, "app", payload, sizeof(payload), tag, &run, &event));
}

typedef struct {
    size_t commands;
    bool cancelled;
} PoisonAppEndpointFixture;

static bool poison_app_endpoint_command(const PoisonAppCommand* command, void* context) {
    PoisonAppEndpointFixture* fixture = context;
    fixture->commands++;
    fixture->cancelled = command->cancel;
    return strcmp(command->command_id, command->cancel ? "cancel" : "run") == 0;
}

MU_TEST(poison_app_endpoint_dispatches_typed_events_and_terminal_cancel) {
    PoisonAppEndpointFixture fixture = {0};
    mu_check(poison_app_endpoint_register(
        "org.structured", "run-typed", poison_app_endpoint_command, &fixture));
    PoisonAppCommand command = {
        .protocol_version = POISON_APP_PROTOCOL_VERSION,
        .app_id = "org.structured",
        .run_id = "run-typed",
        .command_id = "run",
        .payload_json = "{}",
    };
    mu_check(poison_app_dispatch_command(&command));

    PoisonAppEvent event = {0};
    strcpy(event.app_id, "org.structured");
    strcpy(event.run_id, "run-typed");
    strcpy(event.event_id, "log-0");
    event.kind = PoisonAppEventLog;
    event.level = "info";
    strcpy(event.message, "ready");
    mu_check(poison_app_publish_event(&event));

    event.sequence = 1u;
    strcpy(event.event_id, "progress-1");
    event.kind = PoisonAppEventProgress;
    event.percent = 101u;
    event.label = "working";
    mu_check(!poison_app_publish_event(&event));
    event.percent = 50u;
    mu_check(poison_app_publish_event(&event));

    event.sequence = 2u;
    strcpy(event.event_id, "form-2");
    event.kind = PoisonAppEventForm;
    event.schema_json = "{\"type\":\"object\"}";
    mu_check(poison_app_publish_event(&event));

    event.sequence = 3u;
    strcpy(event.event_id, "table-3");
    event.kind = PoisonAppEventTable;
    event.schema_json = "{\"columns\":[\"value\"]}";
    event.rows_json = "[[1]]";
    mu_check(poison_app_publish_event(&event));

    event.sequence = 4u;
    strcpy(event.event_id, "artifact-4");
    event.kind = PoisonAppEventArtifact;
    event.artifact_name = "result.txt";
    event.artifact_path = "/ext/apps_data/org.structured/result.txt";
    event.artifact_sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    event.artifact_size = 42u;
    mu_check(poison_app_publish_event(&event));

    event.sequence = 5u;
    event.artifact_sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeG";
    mu_check(!poison_app_publish_event(&event));
    event.artifact_sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    event.artifact_path = "/ext/apps_data/org.structured/../secret.txt";
    mu_check(!poison_app_publish_event(&event));
    event.artifact_path = "/ext/apps_data/org.structured/result.txt";

    event.sequence = 5u;
    strcpy(event.event_id, "result-5");
    event.kind = PoisonAppEventResult;
    event.success = true;
    strcpy(event.message, "complete");
    mu_check(poison_app_publish_event(&event));

    command.command_id = "cancel";
    command.cancel = true;
    mu_check(poison_app_dispatch_command(&command));
    mu_check(fixture.commands == 2u);
    mu_check(fixture.cancelled);
    mu_check(!poison_app_dispatch_command(&command));
    poison_app_endpoint_unregister(&fixture);
}

MU_TEST_SUITE(poison_app_protocol_suite) {
    MU_RUN_TEST(poison_app_accepts_ordered_events_and_rejects_malformed_events);
    MU_RUN_TEST(poison_app_cancel_is_terminal);
    MU_RUN_TEST(poison_app_rpc_enforces_wire_identifier_capacity);
    MU_RUN_TEST(poison_app_rpc_requires_authenticated_envelope);
    MU_RUN_TEST(poison_app_endpoint_dispatches_typed_events_and_terminal_cancel);
}

void poison_app_protocol_run_tests(void) {
    MU_RUN_SUITE(poison_app_protocol_suite);
}
