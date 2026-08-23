#include "poison_usb_hid_adapter.h"

#include <furi.h>
#include <furi_hal.h>

PoisonUsbHidHandle* poison_usb_hid_open(void) {
    PoisonUsbHidHandle* handle = malloc(sizeof(PoisonUsbHidHandle));
    if(!handle) return NULL;
    handle->previous = furi_hal_usb_get_config();
    handle->active = false;
    furi_hal_usb_unlock();
    if(!furi_hal_usb_set_config(&usb_hid, NULL)) {
        free(handle);
        return NULL;
    }
    handle->active = true;
    return handle;
}

void poison_usb_hid_close(PoisonUsbHidHandle* handle) {
    if(!handle) return;
    poison_usb_hid_release_all();
    if(handle->active) furi_hal_usb_set_config(handle->previous, NULL);
    free(handle);
}

bool poison_usb_hid_connected(void) {
    return furi_hal_hid_is_connected();
}
bool poison_usb_hid_key_press(uint16_t key) {
    return furi_hal_hid_kb_press(key);
}
bool poison_usb_hid_key_release(uint16_t key) {
    return furi_hal_hid_kb_release(key);
}
bool poison_usb_hid_mouse_move(int8_t dx, int8_t dy) {
    return furi_hal_hid_mouse_move(dx, dy);
}
bool poison_usb_hid_mouse_press(uint8_t button) {
    return furi_hal_hid_mouse_press(button);
}
bool poison_usb_hid_mouse_release(uint8_t button) {
    return furi_hal_hid_mouse_release(button);
}
bool poison_usb_hid_mouse_scroll(int8_t delta) {
    return furi_hal_hid_mouse_scroll(delta);
}
bool poison_usb_hid_consumer_press(uint16_t key) {
    return furi_hal_hid_consumer_key_press(key);
}
bool poison_usb_hid_consumer_release(uint16_t key) {
    return furi_hal_hid_consumer_key_release(key);
}
void poison_usb_hid_release_all(void) {
    furi_hal_hid_kb_release_all();
    furi_hal_hid_consumer_key_release_all();
    furi_hal_hid_mouse_release(HID_MOUSE_BTN_LEFT);
    furi_hal_hid_mouse_release(HID_MOUSE_BTN_RIGHT);
}
