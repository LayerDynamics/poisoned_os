#include "../../services/poison_profiles/poison_profiles.h"
#include "../../services/poison_profiles/poison_profiles_i.h"

#include <dialogs/dialogs.h>
#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/view_dispatcher.h>

void poison_settings_describe_profile(const PoisonProfile* profile, FuriString* output);
void poison_settings_describe_changes(
    const PoisonProfile* before,
    const PoisonProfile* after,
    FuriString* output);

typedef enum {
    PoisonSettingsMenuInspect,
    PoisonSettingsMenuPreviewRecovery,
    PoisonSettingsMenuApplyPreview,
    PoisonSettingsMenuResetRecovery,
} PoisonSettingsMenuItem;

typedef struct {
    Gui* gui;
    DialogsApp* dialogs;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    TextBox* text_box;
    FuriString* text;
    bool showing_text;
} PoisonSettingsApp;

static bool poison_settings_confirm(PoisonSettingsApp* app, const char* header, const char* text) {
    DialogMessage* message = dialog_message_alloc();
    dialog_message_set_header(message, header, 64, 0, AlignCenter, AlignTop);
    dialog_message_set_text(message, text, 64, 31, AlignCenter, AlignCenter);
    dialog_message_set_buttons(message, "Cancel", NULL, "Apply");
    const bool approved = dialog_message_show(app->dialogs, message) == DialogMessageButtonRight;
    dialog_message_free(message);
    return approved;
}

static void poison_settings_show_text(PoisonSettingsApp* app) {
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text));
    text_box_set_focus(app->text_box, TextBoxFocusStart);
    app->showing_text = true;
    view_dispatcher_switch_to_view(app->dispatcher, 1u);
}

static void poison_settings_show_result(PoisonSettingsApp* app, const char* result) {
    furi_string_set(app->text, result);
    poison_settings_show_text(app);
}

static void poison_settings_menu_callback(void* context, uint32_t index) {
    PoisonSettingsApp* app = context;
    PoisonProfile* active = malloc(sizeof(*active));
    PoisonProfile* preview = malloc(sizeof(*preview));
    furi_check(active);
    furi_check(preview);
    bool preview_valid = false;
    if(!poison_profiles_copy_global(active, preview, &preview_valid)) {
        free(preview);
        free(active);
        poison_settings_show_result(app, "Profile service unavailable.\n");
        return;
    }
    switch((PoisonSettingsMenuItem)index) {
    case PoisonSettingsMenuInspect:
        poison_settings_describe_profile(active, app->text);
        poison_settings_show_text(app);
        break;
    case PoisonSettingsMenuPreviewRecovery: {
        PoisonProfileStore* recovery = malloc(sizeof(*recovery));
        furi_check(recovery);
        poison_profile_store_init(recovery);
        if(poison_profiles_preview_global(&recovery->active, UINT64_MAX)) {
            poison_settings_describe_changes(active, &recovery->active, app->text);
            poison_settings_show_text(app);
        } else {
            poison_settings_show_result(
                app, "Known-good preview rejected.\nActive profile unchanged.\n");
        }
        memset(recovery, 0, sizeof(*recovery));
        free(recovery);
        break;
    }
    case PoisonSettingsMenuApplyPreview:
        if(!preview_valid) {
            poison_settings_show_result(app, "No validated preview.\nPreview a profile first.\n");
        } else if(
            poison_settings_confirm(app, "Apply profile?", preview->id) &&
            poison_profiles_apply_global()) {
            poison_settings_show_result(app, "Profile applied atomically.\n");
        } else {
            poison_settings_show_result(app, "Profile unchanged.\n");
        }
        break;
    case PoisonSettingsMenuResetRecovery:
        if(poison_settings_confirm(
               app, "Reset UI profile?", "Restore immutable\nknown-good profile") &&
           poison_profiles_reset_global()) {
            poison_settings_show_result(
                app, "Known-good profile restored.\nUser data preserved.\n");
        } else {
            poison_settings_show_result(app, "Profile unchanged.\n");
        }
        break;
    }
    memset(active, 0, sizeof(*active));
    memset(preview, 0, sizeof(*preview));
    free(preview);
    free(active);
}

static bool poison_settings_back_callback(void* context) {
    PoisonSettingsApp* app = context;
    if(app->showing_text) {
        app->showing_text = false;
        view_dispatcher_switch_to_view(app->dispatcher, 0u);
    } else {
        view_dispatcher_stop(app->dispatcher);
    }
    return true;
}

int32_t poison_settings_app(void* context) {
    UNUSED(context);
    PoisonSettingsApp* app = malloc(sizeof(*app));
    furi_check(app);
    memset(app, 0, sizeof(*app));
    app->gui = furi_record_open(RECORD_GUI);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->dispatcher = view_dispatcher_alloc();
    app->menu = submenu_alloc();
    app->text_box = text_box_alloc();
    app->text = furi_string_alloc();
    submenu_set_header(app->menu, "Profiles & Appearance");
    submenu_add_item(
        app->menu,
        "Inspect active profile",
        PoisonSettingsMenuInspect,
        poison_settings_menu_callback,
        app);
    submenu_add_item(
        app->menu,
        "Preview known-good",
        PoisonSettingsMenuPreviewRecovery,
        poison_settings_menu_callback,
        app);
    submenu_add_item(
        app->menu,
        "Apply validated preview",
        PoisonSettingsMenuApplyPreview,
        poison_settings_menu_callback,
        app);
    submenu_add_item(
        app->menu,
        "Reset to known-good",
        PoisonSettingsMenuResetRecovery,
        poison_settings_menu_callback,
        app);
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, poison_settings_back_callback);
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_add_view(app->dispatcher, 0u, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->dispatcher, 1u, text_box_get_view(app->text_box));
    view_dispatcher_switch_to_view(app->dispatcher, 0u);
    view_dispatcher_run(app->dispatcher);
    view_dispatcher_remove_view(app->dispatcher, 1u);
    view_dispatcher_remove_view(app->dispatcher, 0u);
    furi_string_free(app->text);
    text_box_free(app->text_box);
    submenu_free(app->menu);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_GUI);
    memset(app, 0, sizeof(*app));
    free(app);
    return 0;
}
