#include <furi.h>
#include <furi_hal.h>

#include <expansion/expansion.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/text_input.h>
#include <gui/view_dispatcher.h>
#include <dialogs/dialogs.h>
#include <applications/drivers/esp32marauder/esp32_marauder_driver.h>
#include "../../services/poison_tools/poison_marauder_adapter.h"

#define POISON_MARAUDER_BAUD        115200U
#define POISON_MARAUDER_RX_CAPACITY 1024U
#define POISON_MARAUDER_LOG_LIMIT   4096U
#define POISON_MARAUDER_LOG_RETAIN  3072U

typedef enum {
    PoisonMarauderViewMenu,
    PoisonMarauderViewConsole,
    PoisonMarauderViewInput,
} PoisonMarauderView;

typedef struct {
    Expansion* expansion;
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    TextBox* console;
    TextInput* input;
    FuriString* console_text;
    FuriStreamBuffer* rx_stream;
    FuriHalSerialHandle* serial;
    bool console_visible;
    bool input_visible;
    bool otg_was_enabled;
    const Esp32MarauderCommandDescriptor* pending_command;
    char input_buffer[ESP32_MARAUDER_ARGUMENT_MAX + 1u];
    volatile uint32_t dropped_bytes;
} PoisonMarauderApp;

static void poison_marauder_uart_rx(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    PoisonMarauderApp* app = context;
    if(event == FuriHalSerialRxEventData) {
        const uint8_t byte = furi_hal_serial_async_rx(handle);
        if(furi_stream_buffer_send(app->rx_stream, &byte, 1, 0) != 1) {
            app->dropped_bytes++;
        }
    }
}

static void poison_marauder_send(PoisonMarauderApp* app, const char* command) {
    furi_hal_serial_tx(app->serial, (const uint8_t*)command, strlen(command));
    furi_hal_serial_tx_wait_complete(app->serial);
}

static void poison_marauder_trim_log(PoisonMarauderApp* app) {
    const size_t size = furi_string_size(app->console_text);
    if(size > POISON_MARAUDER_LOG_LIMIT) {
        furi_string_right(app->console_text, size - POISON_MARAUDER_LOG_RETAIN);
        furi_string_cat_str(app->console_text, "\n[earlier output truncated]\n");
    }
}

static void poison_marauder_tick(void* context) {
    PoisonMarauderApp* app = context;
    uint8_t buffer[128];
    bool changed = false;

    size_t received;
    while((received = furi_stream_buffer_receive(app->rx_stream, buffer, sizeof(buffer), 0)) > 0) {
        furi_string_cat_printf(app->console_text, "%.*s", (int)received, (char*)buffer);
        changed = true;
    }

    const uint32_t dropped = app->dropped_bytes;
    if(dropped) {
        app->dropped_bytes -= dropped;
        furi_string_cat_printf(app->console_text, "\n[RX overrun: %lu bytes dropped]\n", dropped);
        changed = true;
    }

    if(changed) {
        poison_marauder_trim_log(app);
        text_box_set_text(app->console, furi_string_get_cstr(app->console_text));
    }
}

static bool poison_marauder_navigation(void* context) {
    PoisonMarauderApp* app = context;
    if(app->console_visible || app->input_visible) {
        app->console_visible = false;
        app->input_visible = false;
        app->pending_command = NULL;
        view_dispatcher_switch_to_view(app->view_dispatcher, PoisonMarauderViewMenu);
    } else {
        view_dispatcher_stop(app->view_dispatcher);
    }
    return true;
}

static bool poison_marauder_confirm_command(
    const Esp32MarauderCommandDescriptor* descriptor,
    const char* encoded) {
    if(descriptor->capability != Esp32MarauderCapabilityActive &&
       descriptor->capability != Esp32MarauderCapabilityAdmin) {
        return true;
    }
    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, "Execute exact command?", 64, 0, AlignCenter, AlignTop);
    dialog_message_set_text(message, encoded, 64, 31, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, "Reject", NULL, "Execute");
    const bool approved = dialog_message_show(dialogs, message) == DialogMessageButtonRight;
    dialog_message_free(message);
    furi_record_close(RECORD_DIALOGS);
    return approved;
}

static void poison_marauder_show_command(
    PoisonMarauderApp* app,
    const Esp32MarauderCommandDescriptor* descriptor,
    const char* argument) {
    char command[ESP32_MARAUDER_COMMAND_MAX];
    if(!esp32_marauder_command_format(descriptor->id, argument, command, sizeof(command))) {
        furi_string_set(app->console_text, "POISON MARAUDER | INVALID COMMAND\n");
        text_box_set_text(app->console, furi_string_get_cstr(app->console_text));
        app->console_visible = true;
        app->input_visible = false;
        view_dispatcher_switch_to_view(app->view_dispatcher, PoisonMarauderViewConsole);
        return;
    }
    if(!poison_marauder_confirm_command(descriptor, command)) return;
    furi_string_printf(app->console_text, "POISON MARAUDER | %s\n\n", descriptor->label);
    text_box_set_text(app->console, furi_string_get_cstr(app->console_text));
    text_box_set_focus(app->console, TextBoxFocusEnd);
    app->console_visible = true;
    app->input_visible = false;
    view_dispatcher_switch_to_view(app->view_dispatcher, PoisonMarauderViewConsole);
    poison_marauder_send(app, command);
}

static void poison_marauder_input_complete(void* context) {
    PoisonMarauderApp* app = context;
    furi_check(app->pending_command);
    const Esp32MarauderCommandDescriptor* descriptor = app->pending_command;
    app->pending_command = NULL;
    poison_marauder_show_command(app, descriptor, app->input_buffer);
}

static void poison_marauder_action(void* context, uint32_t index) {
    PoisonMarauderApp* app = context;
    const Esp32MarauderCommandDescriptor* descriptor = esp32_marauder_command_at(index);
    if(!descriptor) return;
    if(descriptor->argument_required) {
        app->pending_command = descriptor;
        app->input_buffer[0] = '\0';
        text_input_reset(app->input);
        text_input_set_header_text(app->input, descriptor->label);
        text_input_set_minimum_length(app->input, 1u);
        text_input_set_result_callback(
            app->input,
            poison_marauder_input_complete,
            app,
            app->input_buffer,
            sizeof(app->input_buffer),
            true);
        app->input_visible = true;
        app->console_visible = false;
        view_dispatcher_switch_to_view(app->view_dispatcher, PoisonMarauderViewInput);
    } else {
        poison_marauder_show_command(app, descriptor, NULL);
    }
}

static PoisonMarauderApp* poison_marauder_alloc(void) {
    PoisonMarauderApp* app = malloc(sizeof(PoisonMarauderApp));
    app->console_visible = false;
    app->input_visible = false;
    app->dropped_bytes = 0;
    app->serial = NULL;
    app->pending_command = NULL;
    app->input_buffer[0] = '\0';

    app->expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(app->expansion);

    app->otg_was_enabled = furi_hal_power_is_otg_enabled();
    if(!app->otg_was_enabled) {
        furi_hal_power_enable_otg();
        furi_delay_ms(200);
    }

    app->rx_stream = furi_stream_buffer_alloc(POISON_MARAUDER_RX_CAPACITY, 1);
    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    furi_check(app->serial);
    furi_hal_serial_init(app->serial, POISON_MARAUDER_BAUD);
    furi_hal_serial_async_rx_start(app->serial, poison_marauder_uart_rx, app, false);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, poison_marauder_navigation);
    view_dispatcher_set_tick_event_callback(app->view_dispatcher, poison_marauder_tick, 100);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->submenu = submenu_alloc();
    submenu_set_header(app->submenu, "POISON MARAUDER");
    for(size_t index = 0; index < esp32_marauder_command_count(); ++index) {
        const Esp32MarauderCommandDescriptor* descriptor = esp32_marauder_command_at(index);
        submenu_add_item(app->submenu, descriptor->label, index, poison_marauder_action, app);
    }

    app->console = text_box_alloc();
    text_box_set_font(app->console, TextBoxFontText);
    app->console_text = furi_string_alloc_set("POISON MARAUDER | READY\n");
    text_box_set_text(app->console, furi_string_get_cstr(app->console_text));
    app->input = text_input_alloc();

    view_dispatcher_add_view(
        app->view_dispatcher, PoisonMarauderViewMenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->view_dispatcher, PoisonMarauderViewConsole, text_box_get_view(app->console));
    view_dispatcher_add_view(
        app->view_dispatcher, PoisonMarauderViewInput, text_input_get_view(app->input));
    return app;
}

static void poison_marauder_free(PoisonMarauderApp* app) {
    char stop_command[ESP32_MARAUDER_COMMAND_MAX];
    if(esp32_marauder_command_format("stop", NULL, stop_command, sizeof(stop_command))) {
        poison_marauder_send(app, stop_command);
    }
    furi_hal_serial_async_rx_stop(app->serial);
    furi_hal_serial_deinit(app->serial);
    furi_hal_serial_control_release(app->serial);

    view_dispatcher_remove_view(app->view_dispatcher, PoisonMarauderViewInput);
    view_dispatcher_remove_view(app->view_dispatcher, PoisonMarauderViewConsole);
    view_dispatcher_remove_view(app->view_dispatcher, PoisonMarauderViewMenu);
    view_dispatcher_free(app->view_dispatcher);
    text_box_free(app->console);
    text_input_free(app->input);
    submenu_free(app->submenu);
    furi_string_free(app->console_text);
    furi_stream_buffer_free(app->rx_stream);
    furi_record_close(RECORD_GUI);

    if(!app->otg_was_enabled && furi_hal_power_is_otg_enabled()) {
        furi_hal_power_disable_otg();
    }
    expansion_enable(app->expansion);
    furi_record_close(RECORD_EXPANSION);
    free(app);
}

int32_t poison_marauder_app(void* context) {
    UNUSED(context);
    PoisonMarauderApp* app = poison_marauder_alloc();
    view_dispatcher_switch_to_view(app->view_dispatcher, PoisonMarauderViewMenu);
    view_dispatcher_run(app->view_dispatcher);
    poison_marauder_free(app);
    return 0;
}
