#include "poison_ble_adapter.h"

#include <furi.h>
#include <furi_hal.h>
#include <core/record.h>
#include <bt/bt_service/bt.h>
#include <extra_profiles/hid_profile.h>

PoisonBleHandle* poison_ble_open(void) {
    PoisonBleHandle* handle = malloc(sizeof(PoisonBleHandle));
    if(handle) {
        handle->bt = furi_record_open(RECORD_BT);
        handle->profile = bt_profile_start(handle->bt, ble_profile_hid, NULL);
        handle->advertising = false;
        if(!handle->profile) {
            furi_record_close(RECORD_BT);
            free(handle);
            return NULL;
        }
    }
    return handle;
}

void poison_ble_close(PoisonBleHandle* handle) {
    if(!handle) return;
    poison_ble_release_all(handle);
    poison_ble_stop_advertising(handle);
    bt_profile_restore_default(handle->bt);
    furi_record_close(RECORD_BT);
    free(handle);
}

bool poison_ble_start_advertising(PoisonBleHandle* handle) {
    if(!handle || handle->advertising || !furi_hal_bt_is_alive()) return false;
    furi_hal_bt_start_advertising();
    handle->advertising = true;
    return true;
}

void poison_ble_stop_advertising(PoisonBleHandle* handle) {
    if(!handle || !handle->advertising) return;
    furi_hal_bt_stop_advertising();
    handle->advertising = false;
}

bool poison_ble_is_active(void) {
    return furi_hal_bt_is_active();
}

bool poison_ble_key_press(PoisonBleHandle* handle, uint16_t key) {
    return handle && handle->profile && ble_profile_hid_kb_press(handle->profile, key);
}

bool poison_ble_key_release(PoisonBleHandle* handle, uint16_t key) {
    return handle && handle->profile && ble_profile_hid_kb_release(handle->profile, key);
}

bool poison_ble_mouse_move(PoisonBleHandle* handle, int8_t dx, int8_t dy) {
    return handle && handle->profile && ble_profile_hid_mouse_move(handle->profile, dx, dy);
}

bool poison_ble_mouse_press(PoisonBleHandle* handle, uint8_t button) {
    return handle && handle->profile && ble_profile_hid_mouse_press(handle->profile, button);
}

bool poison_ble_mouse_release(PoisonBleHandle* handle, uint8_t button) {
    return handle && handle->profile && ble_profile_hid_mouse_release(handle->profile, button);
}

bool poison_ble_mouse_scroll(PoisonBleHandle* handle, int8_t delta) {
    return handle && handle->profile && ble_profile_hid_mouse_scroll(handle->profile, delta);
}

bool poison_ble_consumer_press(PoisonBleHandle* handle, uint16_t key) {
    return handle && handle->profile && ble_profile_hid_consumer_key_press(handle->profile, key);
}

bool poison_ble_consumer_release(PoisonBleHandle* handle, uint16_t key) {
    return handle && handle->profile && ble_profile_hid_consumer_key_release(handle->profile, key);
}

void poison_ble_release_all(PoisonBleHandle* handle) {
    if(!handle || !handle->profile) return;
    ble_profile_hid_kb_release_all(handle->profile);
    ble_profile_hid_consumer_key_release_all(handle->profile);
    ble_profile_hid_mouse_release_all(handle->profile);
}
