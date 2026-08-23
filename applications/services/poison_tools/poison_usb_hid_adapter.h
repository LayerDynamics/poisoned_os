#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <furi_hal_usb.h>

typedef struct {
    FuriHalUsbInterface* previous;
    bool active;
} PoisonUsbHidHandle;

#ifdef __cplusplus
extern "C" {
#endif

PoisonUsbHidHandle* poison_usb_hid_open(void);
void poison_usb_hid_close(PoisonUsbHidHandle* handle);
bool poison_usb_hid_connected(void);
bool poison_usb_hid_key_press(uint16_t key);
bool poison_usb_hid_key_release(uint16_t key);
bool poison_usb_hid_mouse_move(int8_t dx, int8_t dy);
bool poison_usb_hid_mouse_press(uint8_t button);
bool poison_usb_hid_mouse_release(uint8_t button);
bool poison_usb_hid_mouse_scroll(int8_t delta);
bool poison_usb_hid_consumer_press(uint16_t key);
bool poison_usb_hid_consumer_release(uint16_t key);
void poison_usb_hid_release_all(void);

#ifdef __cplusplus
}
#endif
