#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/view_dispatcher.h>
#include <loader/loader.h>
#include <string.h>

typedef struct {
    const char* label;
    const char* application;
    const char* arguments;
} PoisonToolboxEntry;

static const PoisonToolboxEntry poison_toolbox_entries[] = {
    {"NFC", "NFC", NULL},
    {"LF RFID", "125 kHz RFID", NULL},
    {"iButton", "iButton", NULL},
    {"Infrared", "Infrared", NULL},
    {"Sub-GHz", "Sub-GHz", NULL},
    {"GPIO inspect", "GPIO", NULL},
    {"USB HID", "Bad USB", NULL},
    {"BLE status", "Bluetooth", NULL},
    {"Serial console", "GPIO", NULL},
    {"Storage", "Archive", NULL},
    {"Marauder", "/ext/apps/GPIO/poison_marauder.fap", NULL},
    {"ESP Flasher", "/ext/apps/GPIO/poison_esp_flasher.fap", "select_board"},
};

typedef struct {
    Gui* gui;
    Loader* loader;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
} PoisonToolbox;

static void poison_toolbox_select(void* context, uint32_t index) {
    PoisonToolbox* app = context;
    if(index >= COUNT_OF(poison_toolbox_entries)) return;
    const PoisonToolboxEntry* entry = &poison_toolbox_entries[index];
    loader_enqueue_launch(
        app->loader, entry->application, entry->arguments, LoaderDeferredLaunchFlagGui);
    view_dispatcher_stop(app->view_dispatcher);
}

static bool poison_toolbox_back(void* context) {
    PoisonToolbox* app = context;
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

int32_t poison_toolbox_app(void* context) {
    UNUSED(context);
    PoisonToolbox* app = malloc(sizeof(*app));
    furi_check(app);
    memset(app, 0, sizeof(*app));
    app->gui = furi_record_open(RECORD_GUI);
    app->loader = furi_record_open(RECORD_LOADER);
    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, poison_toolbox_back);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_add_view(app->view_dispatcher, 0u, submenu_get_view(app->submenu));
    submenu_set_header(app->submenu, "Poison Tools");
    for(size_t index = 0u; index < COUNT_OF(poison_toolbox_entries); index++) {
        submenu_add_item(
            app->submenu, poison_toolbox_entries[index].label, index, poison_toolbox_select, app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, 0u);
    view_dispatcher_run(app->view_dispatcher);

    view_dispatcher_remove_view(app->view_dispatcher, 0u);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_LOADER);
    furi_record_close(RECORD_GUI);
    free(app);
    return 0;
}
