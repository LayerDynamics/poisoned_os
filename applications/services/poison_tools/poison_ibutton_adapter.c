#include "poison_ibutton_adapter.h"

#include <furi.h>
#include <furi_hal_rfid.h>
#include <power/power_service/power.h>
#include <string.h>

static void poison_ibutton_otg(bool enabled) {
    Power* power = furi_record_open(RECORD_POWER);
    power_enable_otg(power, enabled);
    furi_record_close(RECORD_POWER);
}

PoisonIbuttonHandle* poison_ibutton_open(void) {
    PoisonIbuttonHandle* handle = malloc(sizeof(PoisonIbuttonHandle));
    if(!handle) return NULL;
    handle->protocols = ibutton_protocols_alloc();
    handle->key = ibutton_key_alloc(ibutton_protocols_get_max_data_size(handle->protocols));
    handle->emulating = false;
    if(!handle->protocols || !handle->key) {
        poison_ibutton_close(handle);
        return NULL;
    }
    return handle;
}

void poison_ibutton_close(PoisonIbuttonHandle* handle) {
    if(!handle) return;
    if(handle->emulating) poison_ibutton_emulate_stop(handle);
    if(handle->key) ibutton_key_free(handle->key);
    if(handle->protocols) ibutton_protocols_free(handle->protocols);
    free(handle);
}

bool poison_ibutton_read(PoisonIbuttonHandle* handle, PoisonIbuttonReadResult* result) {
    if(!handle || !handle->protocols || !handle->key || !result) return false;
    memset(result, 0, sizeof(*result));
    poison_ibutton_otg(true);
    bool read = ibutton_protocols_read(handle->protocols, handle->key);
    poison_ibutton_otg(false);
    if(!read) return false;
    result->protocol = ibutton_key_get_protocol_id(handle->key);
    const char* protocol_name = ibutton_protocols_get_name(handle->protocols, result->protocol);
    if(!protocol_name ||
       strnlen(protocol_name, sizeof(result->protocol_name)) >= sizeof(result->protocol_name)) {
        return false;
    }
    strcpy(result->protocol_name, protocol_name);
    iButtonEditableData editable = {0};
    ibutton_protocols_get_editable_data(handle->protocols, handle->key, &editable);
    if(!editable.ptr || editable.size == 0u || editable.size > sizeof(result->data)) return false;
    memcpy(result->data, editable.ptr, editable.size);
    result->data_size = editable.size;
    result->valid = ibutton_protocols_is_valid(handle->protocols, handle->key);
    FuriString* rendered = furi_string_alloc();
    ibutton_protocols_render_brief_data(handle->protocols, handle->key, rendered);
    const size_t size = furi_string_size(rendered);
    if(size >= sizeof(result->rendered)) {
        furi_string_free(rendered);
        return false;
    }
    memcpy(result->rendered, furi_string_get_cstr(rendered), size + 1u);
    result->rendered_size = size;
    furi_string_free(rendered);
    return true;
}

bool poison_ibutton_write_id(PoisonIbuttonHandle* handle) {
    if(!handle || !handle->protocols || !handle->key) return false;
    poison_ibutton_otg(true);
    bool written = ibutton_protocols_write_id(handle->protocols, handle->key);
    poison_ibutton_otg(false);
    return written;
}

bool poison_ibutton_emulate_start(PoisonIbuttonHandle* handle) {
    if(!handle || !handle->protocols || !handle->key || handle->emulating) return false;
    furi_hal_rfid_pins_reset();
    furi_hal_rfid_pin_pull_pulldown();
    ibutton_protocols_emulate_start(handle->protocols, handle->key);
    handle->emulating = true;
    return true;
}

void poison_ibutton_emulate_stop(PoisonIbuttonHandle* handle) {
    if(!handle || !handle->emulating) return;
    ibutton_protocols_emulate_stop(handle->protocols, handle->key);
    furi_hal_rfid_pins_reset();
    handle->emulating = false;
}
