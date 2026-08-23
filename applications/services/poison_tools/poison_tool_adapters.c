#include "poison_tool_adapters.h"

#include <string.h>

typedef struct {
    const char* key;
    PoisonToolAdapter adapter;
    const char* capability;
} AdapterEntry;

static const AdapterEntry family_entries[] = {
    {"nfc", PoisonToolAdapterNfc, "nfc.read"},
    {"lf-rfid", PoisonToolAdapterLfRfid, "lf-rfid.read"},
    {"ibutton", PoisonToolAdapterIbutton, "ibutton.read"},
    {"infrared", PoisonToolAdapterInfrared, "infrared.receive"},
    {"sub-ghz", PoisonToolAdapterSubGhz, "sub-ghz.receive"},
    {"gpio", PoisonToolAdapterGpio, "gpio.read"},
    {"usb-hid", PoisonToolAdapterUsbHid, "usb-hid.inspect"},
    {"ble-hid", PoisonToolAdapterBleHid, "ble.status"},
    {"serial", PoisonToolAdapterSerial, "serial.observe"},
    {"storage", PoisonToolAdapterStorage, "storage.read"},
};

static const AdapterEntry tool_entries[] = {
    {"marauder.console", PoisonToolAdapterMarauder, "marauder.observe"},
    {"esp-flasher", PoisonToolAdapterEspFlasher, "esp.flash"},
};

static bool poison_gpio_capability(const char* capability) {
    static const char* const capabilities[] = {"gpio.read", "gpio.write", "gpio.sample"};
    for(size_t i = 0; i < sizeof(capabilities) / sizeof(capabilities[0]); i++) {
        if(strcmp(capability, capabilities[i]) == 0) return true;
    }
    return false;
}

bool poison_tool_adapter_for_family(const char* family, PoisonToolAdapter* adapter) {
    if(!family || !adapter) return false;
    for(size_t i = 0; i < sizeof(family_entries) / sizeof(family_entries[0]); i++) {
        if(strcmp(family_entries[i].key, family) == 0) {
            *adapter = family_entries[i].adapter;
            return true;
        }
    }
    return false;
}

bool poison_tool_adapter_for_tool(
    const char* tool_id,
    const char* family,
    PoisonToolAdapter* adapter) {
    if(!tool_id || !family || !adapter) return false;
    for(size_t i = 0; i < sizeof(tool_entries) / sizeof(tool_entries[0]); i++) {
        if(strcmp(tool_entries[i].key, tool_id) == 0) {
            *adapter = tool_entries[i].adapter;
            return true;
        }
    }
    return poison_tool_adapter_for_family(family, adapter);
}

bool poison_tool_adapter_supports_capability(PoisonToolAdapter adapter, const char* capability) {
    if(!capability) return false;
    if(adapter == PoisonToolAdapterGpio) return poison_gpio_capability(capability);
    static const struct {
        PoisonToolAdapter adapter;
        const char* capability;
    } expanded[] = {
        {PoisonToolAdapterIbutton, "ibutton.read"},
        {PoisonToolAdapterIbutton, "ibutton.write"},
        {PoisonToolAdapterIbutton, "ibutton.emulate"},
        {PoisonToolAdapterInfrared, "infrared.receive"},
        {PoisonToolAdapterInfrared, "infrared.transmit"},
        {PoisonToolAdapterSubGhz, "sub-ghz.receive"},
        {PoisonToolAdapterSubGhz, "sub-ghz.transmit"},
        {PoisonToolAdapterSubGhz, "sub-ghz.transmit.complete"},
        {PoisonToolAdapterUsbHid, "usb-hid.inspect"},
        {PoisonToolAdapterUsbHid, "usb-hid.keyboard"},
        {PoisonToolAdapterUsbHid, "usb-hid.mouse"},
        {PoisonToolAdapterUsbHid, "usb-hid.consumer"},
        {PoisonToolAdapterUsbHid, "usb-hid.scroll"},
        {PoisonToolAdapterBleHid, "ble.status"},
        {PoisonToolAdapterBleHid, "ble.advertise"},
        {PoisonToolAdapterBleHid, "ble.keyboard"},
        {PoisonToolAdapterBleHid, "ble.mouse"},
        {PoisonToolAdapterBleHid, "ble.consumer"},
        {PoisonToolAdapterBleHid, "ble.scroll"},
        {PoisonToolAdapterMarauder, "marauder.observe"},
        {PoisonToolAdapterMarauder, "marauder.control"},
        {PoisonToolAdapterMarauder, "marauder.capture"},
        {PoisonToolAdapterMarauder, "marauder.active"},
        {PoisonToolAdapterMarauder, "marauder.admin"},
        {PoisonToolAdapterEspFlasher, "esp.flash"},
    };
    for(size_t i = 0; i < sizeof(expanded) / sizeof(expanded[0]); i++) {
        if(expanded[i].adapter == adapter && strcmp(expanded[i].capability, capability) == 0)
            return true;
    }
    for(size_t i = 0; i < sizeof(family_entries) / sizeof(family_entries[0]); i++) {
        if(family_entries[i].adapter == adapter &&
           strcmp(family_entries[i].capability, capability) == 0)
            return true;
    }
    for(size_t i = 0; i < sizeof(tool_entries) / sizeof(tool_entries[0]); i++) {
        if(tool_entries[i].adapter == adapter &&
           strcmp(tool_entries[i].capability, capability) == 0)
            return true;
    }
    return false;
}
