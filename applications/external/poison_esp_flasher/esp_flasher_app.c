#include "esp_flasher_app_i.h"
#include "../../services/poison_tools/poison_esp_adapter.h"
#include "esp_flasher_worker.h"

#include <furi.h>
#include <furi_hal.h>
#include <expansion/expansion.h>

static bool esp_flasher_app_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    EspFlasherApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool esp_flasher_app_back_event_callback(void* context) {
    furi_assert(context);
    EspFlasherApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void esp_flasher_app_tick_event_callback(void* context) {
    furi_assert(context);
    EspFlasherApp* app = context;
    if(app->automation_mode && app->automation_complete) {
        view_dispatcher_stop(app->view_dispatcher);
        return;
    }
    scene_manager_handle_tick_event(app->scene_manager);
}

EspFlasherApp* esp_flasher_app_alloc() {
    EspFlasherApp* app = malloc(sizeof(EspFlasherApp));
    memset(app, 0, sizeof(EspFlasherApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notification = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&esp_flasher_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);

    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, esp_flasher_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, esp_flasher_app_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, esp_flasher_app_tick_event_callback, 100);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        EspFlasherAppViewVarItemList,
        variable_item_list_get_view(app->var_item_list));

    app->text_box = text_box_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, EspFlasherAppViewConsoleOutput, text_box_get_view(app->text_box));
    app->text_box_store = furi_string_alloc();
    furi_string_reserve(app->text_box_store, ESP_FLASHER_TEXT_BOX_STORE_SIZE);

    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, EspFlasherAppViewWidget, widget_get_view(app->widget));

    // Submenu
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, EspFlasherAppViewSubmenu, submenu_get_view(app->submenu));

    // Text Input (for custom flash offset entry)
    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, EspFlasherAppViewTextInput, text_input_get_view(app->text_input));

    app->flash_worker_busy = false;

    app->reset = false;
    app->boot = false;
    app->quickflash = false;

    app->turbospeed = false;
    app->switch_fw = SwitchNotSet;

    app->pending_addr_slot = -1;
    app->boot_addr_manually_set = false;
    memset(app->custom_slot_addrs, 0, sizeof(app->custom_slot_addrs));
    app->addr_input_str[0] = '\0';
    app->advanced_mode = false;
    memset(app->parsed_slot_addrs, 0, sizeof(app->parsed_slot_addrs));
    app->part_confirm_text[0] = '\0';

    scene_manager_next_scene(app->scene_manager, EspFlasherSceneStart);

    return app;
}

void esp_flasher_configure_marauder_flipper(EspFlasherApp* app) {
    furi_assert(app);
    memset(app->selected_flash_options, 0, sizeof(app->selected_flash_options));
    memset(app->custom_slot_addrs, 0, sizeof(app->custom_slot_addrs));
    app->bin_file_path_boot[0] = '\0';
    app->bin_file_path_part[0] = '\0';
    app->bin_file_path_nvs[0] = '\0';
    app->bin_file_path_boot_app0[0] = '\0';
    app->bin_file_path_app_a[0] = '\0';
    app->bin_file_path_app_b[0] = '\0';
    app->bin_file_path_custom[0] = '\0';

    app->selected_flash_options[SelectedFlashBoot] = true;
    app->selected_flash_options[SelectedFlashPart] = true;
    app->selected_flash_options[SelectedFlashBootApp0] = true;
    app->selected_flash_options[SelectedFlashAppA] = true;
    app->num_selected_flash_options = 4;

    app->custom_slot_addrs[SelectedFlashBoot] = ESP_ADDR_BOOT;
    app->custom_slot_addrs[SelectedFlashPart] = ESP_ADDR_PART;
    app->custom_slot_addrs[SelectedFlashBootApp0] = ESP_ADDR_BOOT_APP0;
    app->custom_slot_addrs[SelectedFlashAppA] = ESP_ADDR_APP_A;

    strlcpy(
        app->bin_file_path_boot,
        APP_DATA_PATH("assets/marauder/s2/esp32_marauder.ino.bootloader.bin"),
        sizeof(app->bin_file_path_boot));
    strlcpy(
        app->bin_file_path_part,
        APP_DATA_PATH("assets/marauder/esp32_marauder.ino.partitions.bin"),
        sizeof(app->bin_file_path_part));
    strlcpy(
        app->bin_file_path_boot_app0,
        APP_DATA_PATH("assets/marauder/boot_app0.bin"),
        sizeof(app->bin_file_path_boot_app0));
    strlcpy(
        app->bin_file_path_app_a,
        APP_DATA_PATH("assets/marauder/s2/esp32_marauder.flipper.bin"),
        sizeof(app->bin_file_path_app_a));

    app->boot = true;
    app->quickflash = true;
    app->turbospeed = true;
    app->switch_fw = SwitchNotSet;
    scene_manager_next_scene(app->scene_manager, EspFlasherSceneConsoleOutput);
}

void esp_flasher_make_app_folder(EspFlasherApp* app) {
    furi_assert(app);

    if(!storage_simply_mkdir(app->storage, ESP_APP_FOLDER)) {
        dialog_message_show_storage_error(app->dialogs, "Cannot create\napp folder");
    }
}

void esp_flasher_app_free(EspFlasherApp* app) {
    furi_assert(app);

    if(app->flash_worker) {
        esp_flasher_worker_stop_thread(app);
    }

    // Views
    view_dispatcher_remove_view(app->view_dispatcher, EspFlasherAppViewVarItemList);
    view_dispatcher_remove_view(app->view_dispatcher, EspFlasherAppViewConsoleOutput);
    view_dispatcher_remove_view(app->view_dispatcher, EspFlasherAppViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, EspFlasherAppViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, EspFlasherAppViewTextInput);

    widget_free(app->widget);
    text_box_free(app->text_box);
    furi_string_free(app->text_box_store);
    submenu_free(app->submenu);
    text_input_free(app->text_input);
    variable_item_list_free(app->var_item_list);

    // View dispatcher
    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    esp_flasher_uart_free(app->uart);

    // Close records
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_NOTIFICATION);

    free(app);
}

int32_t esp_flasher_app(void* p) {
    // Disable expansion protocol to avoid interference with UART Handle
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);

    uint8_t attempts = 0;
    bool otg_was_enabled = furi_hal_power_is_otg_enabled();
    while(!furi_hal_power_is_otg_enabled() && attempts++ < 5) {
        furi_hal_power_enable_otg();
        furi_delay_ms(10);
    }
    furi_delay_ms(200);

    EspFlasherApp* esp_flasher_app = esp_flasher_app_alloc();

    esp_flasher_make_app_folder(esp_flasher_app);

    esp_flasher_app->uart = esp_flasher_usart_init(esp_flasher_app);

    PoisonEspTarget target;
    if(p && poison_esp_target_parse("flipper-zero-wifi-dev-board", &target) &&
       strcmp(p, "marauder_flipper") == 0) {
        esp_flasher_app->automation_mode = true;
        esp_flasher_configure_marauder_flipper(esp_flasher_app);
    } else if(p && strcmp(p, "select_board") == 0) {
        scene_manager_next_scene(esp_flasher_app->scene_manager, EspFlasherSceneQuick);
    }

    view_dispatcher_run(esp_flasher_app->view_dispatcher);

    esp_flasher_app_free(esp_flasher_app);

    if(furi_hal_power_is_otg_enabled() && !otg_was_enabled) {
        furi_hal_power_disable_otg();
    }

    // Return previous state of expansion
    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);

    return 0;
}
