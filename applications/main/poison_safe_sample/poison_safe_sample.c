#include "poison_safe_sample.h"

#include <stdio.h>

#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/view_dispatcher.h>

#include "../../services/poison_app/poison_app_sdk.h"

typedef struct {
    Gui* gui;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    TextBox* output;
    FuriString* text;
    volatile bool cancelled;
    uint64_t structured_sequence;
} PoisonSafeSampleApp;

enum {
    PoisonSafeSampleEventCancel = 1u,
};

static bool poison_safe_sample_publish(
    PoisonSafeSampleApp* app,
    PoisonAppEventKind kind,
    const char* event_id,
    const char* message,
    uint32_t percent) {
    PoisonAppEvent event = {0};
    strcpy(event.app_id, "poison_safe_sample");
    strcpy(event.run_id, "onboarding");
    strcpy(event.event_id, event_id);
    event.sequence = app->structured_sequence;
    event.kind = kind;
    if(kind == PoisonAppEventProgress) {
        event.percent = percent;
        event.label = message;
    } else {
        event.success = true;
        strcpy(event.message, message);
    }
    const bool published = poison_app_sdk_publish(&event);
    if(published) app->structured_sequence++;
    return published;
}

static bool poison_safe_sample_structured_command(const PoisonAppCommand* command, void* context) {
    PoisonSafeSampleApp* app = context;
    if(command->cancel) {
        app->cancelled = true;
        view_dispatcher_send_custom_event(app->dispatcher, PoisonSafeSampleEventCancel);
        return true;
    }
    if(strcmp(command->command_id, "status") == 0) {
        return poison_safe_sample_publish(app, PoisonAppEventResult, "status", "ready", 0u);
    }
    if(strcmp(command->command_id, "run") != 0 || strlen(command->payload_json) != 1u ||
       command->payload_json[0] < '1' || command->payload_json[0] > '3') {
        return false;
    }
    const uint32_t parameter = (uint32_t)(command->payload_json[0] - '0');
    char result[POISON_SAFE_SAMPLE_ARTIFACT_MAX];
    if(!poison_safe_sample_generate_artifact(parameter, result, sizeof(result))) return false;
    return poison_safe_sample_publish(
               app, PoisonAppEventProgress, "progress-start", "running", 0u) &&
           poison_safe_sample_publish(
               app, PoisonAppEventProgress, "progress-complete", "complete", 100u) &&
           poison_safe_sample_publish(app, PoisonAppEventResult, "result", result, 0u);
}

bool poison_safe_sample_validate_parameter(uint32_t parameter) {
    return parameter >= POISON_SAFE_SAMPLE_MIN_PARAMETER &&
           parameter <= POISON_SAFE_SAMPLE_MAX_PARAMETER;
}

size_t
    poison_safe_sample_generate_artifact(uint32_t parameter, char* output, size_t output_capacity) {
    if(!output || output_capacity == 0u || !poison_safe_sample_validate_parameter(parameter))
        return 0u;
    uint32_t checksum = 2166136261u;
    for(uint32_t index = 0; index < parameter; index++) {
        checksum ^= index + 1u;
        checksum *= 16777619u;
    }
    int written = snprintf(
        output,
        output_capacity,
        "POISON-SAFE-SAMPLE:v1:param=%lu:checksum=%08lx",
        (unsigned long)parameter,
        (unsigned long)checksum);
    if(written < 0 || (size_t)written >= output_capacity) {
        output[0] = '\0';
        return 0u;
    }
    return (size_t)written;
}

static bool poison_safe_sample_navigation(void* context) {
    PoisonSafeSampleApp* app = context;
    app->cancelled = true;
    view_dispatcher_stop(app->dispatcher);
    return true;
}

static bool poison_safe_sample_custom_event(void* context, uint32_t event) {
    PoisonSafeSampleApp* app = context;
    if(event != PoisonSafeSampleEventCancel) return false;
    view_dispatcher_stop(app->dispatcher);
    return true;
}

static void poison_safe_sample_run(void* context, uint32_t index) {
    PoisonSafeSampleApp* app = context;
    uint32_t parameter = index + 1u;
    char artifact[POISON_SAFE_SAMPLE_ARTIFACT_MAX];
    if(!poison_safe_sample_generate_artifact(parameter, artifact, sizeof(artifact))) return;
    PoisonAppRun run = {0};
    if(!poison_app_run_start(&run, "poison_safe_sample", "onboarding")) return;
    furi_string_printf(
        app->text, "SAFE SAMPLE\n\nSTATUS: RUNNING\nPARAMETER: %lu\n", (unsigned long)parameter);
    text_box_set_text(app->output, furi_string_get_cstr(app->text));
    view_dispatcher_switch_to_view(app->dispatcher, 1);
    for(uint32_t progress = 0; progress <= parameter && !app->cancelled; progress++) {
        PoisonAppEvent event = {0};
        snprintf(event.app_id, sizeof(event.app_id), "%s", "poison_safe_sample");
        snprintf(event.run_id, sizeof(event.run_id), "%s", "onboarding");
        event.sequence = run.next_sequence;
        event.kind = progress == parameter ? PoisonAppEventResult : PoisonAppEventProgress;
        snprintf(event.message, sizeof(event.message), "progress=%lu", (unsigned long)progress);
        if(!poison_app_accept_event(&run, &event)) break;
        furi_delay_ms(20);
    }
    if(app->cancelled) poison_app_cancel(&run);
    furi_string_cat_printf(
        app->text,
        "\n%s\n%s\n",
        app->cancelled ? "STATUS: CANCELLED" : "STATUS: COMPLETE",
        artifact);
    text_box_set_text(app->output, furi_string_get_cstr(app->text));
}

static PoisonSafeSampleApp* poison_safe_sample_alloc(void) {
    PoisonSafeSampleApp* app = malloc(sizeof(PoisonSafeSampleApp));
    memset(app, 0, sizeof(*app));
    app->cancelled = false;
    app->gui = furi_record_open(RECORD_GUI);
    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, poison_safe_sample_navigation);
    view_dispatcher_set_custom_event_callback(app->dispatcher, poison_safe_sample_custom_event);
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    app->menu = submenu_alloc();
    submenu_set_header(app->menu, "POISON SAFE SAMPLE");
    for(uint32_t parameter = POISON_SAFE_SAMPLE_MIN_PARAMETER; parameter <= 3u; parameter++)
        submenu_add_item(
            app->menu,
            parameter == 1u ? "Run sample (1)" :
            parameter == 2u ? "Run sample (2)" :
                              "Run sample (3)",
            parameter - 1u,
            poison_safe_sample_run,
            app);
    app->output = text_box_alloc();
    app->text = furi_string_alloc_set("SAFE SAMPLE\nSelect a bounded parameter.\n");
    text_box_set_text(app->output, furi_string_get_cstr(app->text));
    view_dispatcher_add_view(app->dispatcher, 0, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->dispatcher, 1, text_box_get_view(app->output));
    return app;
}

static void poison_safe_sample_free(PoisonSafeSampleApp* app) {
    view_dispatcher_remove_view(app->dispatcher, 1);
    view_dispatcher_remove_view(app->dispatcher, 0);
    view_dispatcher_free(app->dispatcher);
    text_box_free(app->output);
    submenu_free(app->menu);
    furi_string_free(app->text);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t poison_safe_sample_app(void* context) {
    UNUSED(context);
    PoisonSafeSampleApp* app = poison_safe_sample_alloc();
    const bool structured = poison_app_sdk_register(
        "poison_safe_sample", "onboarding", poison_safe_sample_structured_command, app);
    view_dispatcher_switch_to_view(app->dispatcher, 0);
    view_dispatcher_run(app->dispatcher);
    if(structured) poison_app_sdk_unregister(app);
    poison_safe_sample_free(app);
    return 0;
}
