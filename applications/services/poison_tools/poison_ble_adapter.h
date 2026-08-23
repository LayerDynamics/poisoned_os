#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <furi_ble/profile_interface.h>
#include <bt/bt_service/bt.h>

typedef struct {
    Bt* bt;
    FuriHalBleProfileBase* profile;
    bool advertising;
} PoisonBleHandle;

#ifdef __cplusplus
extern "C" {
#endif

PoisonBleHandle* poison_ble_open(void);
void poison_ble_close(PoisonBleHandle* handle);
bool poison_ble_start_advertising(PoisonBleHandle* handle);
void poison_ble_stop_advertising(PoisonBleHandle* handle);
bool poison_ble_is_active(void);
bool poison_ble_key_press(PoisonBleHandle* handle, uint16_t key);
bool poison_ble_key_release(PoisonBleHandle* handle, uint16_t key);
bool poison_ble_mouse_move(PoisonBleHandle* handle, int8_t dx, int8_t dy);
bool poison_ble_mouse_press(PoisonBleHandle* handle, uint8_t button);
bool poison_ble_mouse_release(PoisonBleHandle* handle, uint8_t button);
bool poison_ble_mouse_scroll(PoisonBleHandle* handle, int8_t delta);
bool poison_ble_consumer_press(PoisonBleHandle* handle, uint16_t key);
bool poison_ble_consumer_release(PoisonBleHandle* handle, uint16_t key);
void poison_ble_release_all(PoisonBleHandle* handle);

#ifdef __cplusplus
}
#endif
