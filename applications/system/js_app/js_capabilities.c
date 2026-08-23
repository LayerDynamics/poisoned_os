#include "js_capabilities.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    const char* module;
    uint32_t capability;
} JsCapabilityEntry;

static const JsCapabilityEntry entries[] = {
    {"flipper", JsCapabilityDevice},
    {"device", JsCapabilityDevice},
    {"event_loop", JsCapabilityRuntime},
    {"gui", JsCapabilityUi},
    {"gui/loading", JsCapabilityUi},
    {"gui/empty_screen", JsCapabilityUi},
    {"gui/submenu", JsCapabilityUi},
    {"gui/text_input", JsCapabilityUi},
    {"gui/number_input", JsCapabilityUi},
    {"gui/button_panel", JsCapabilityUi},
    {"gui/popup", JsCapabilityUi},
    {"gui/button_menu", JsCapabilityUi},
    {"gui/menu", JsCapabilityUi},
    {"gui/vi_list", JsCapabilityUi},
    {"gui/byte_input", JsCapabilityUi},
    {"gui/text_box", JsCapabilityUi},
    {"gui/dialog", JsCapabilityUi},
    {"gui/file_picker", JsCapabilityUi},
    {"gui/widget", JsCapabilityUi},
    {"gui/icon", JsCapabilityUi},
    {"notification", JsCapabilityNotification},
    {"badusb", JsCapabilityBadUsb},
    {"serial", JsCapabilitySerial},
    {"gpio", JsCapabilityGpio},
    {"math", JsCapabilityCompute},
    {"storage", JsCapabilityStorage},
    {"crypto", JsCapabilityCrypto},
    {"evidence", JsCapabilityEvidence},
#ifdef FW_CFG_unit_tests
    {"tests", JsCapabilityCompute},
#endif
};

uint32_t js_capability_for_module(const char* module) {
    if(!module) return 0u;
    for(size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        if(strcmp(entries[i].module, module) == 0) return entries[i].capability;
    }
    return 0u;
}

bool js_capability_module_known(const char* module) {
    return js_capability_for_module(module) != 0u;
}

bool js_capability_module_allowed(const char* module, uint32_t granted) {
    const uint32_t required = js_capability_for_module(module);
    return required != 0u && (granted & required) == required;
}
