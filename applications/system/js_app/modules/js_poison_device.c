#include "js_poison_device.h"

#include <furi_hal_version.h>
#include <power/power_service/power.h>

static void js_poison_device_get_model(struct mjs* mjs) {
    mjs_return(mjs, mjs_mk_string(mjs, furi_hal_version_get_model_name(), ~0, true));
}

static void js_poison_device_get_name(struct mjs* mjs) {
    const char* name = furi_hal_version_get_name_ptr();
    mjs_return(mjs, mjs_mk_string(mjs, name ? name : "Unknown", ~0, true));
}

static void js_poison_device_get_battery(struct mjs* mjs) {
    Power* power = furi_record_open(RECORD_POWER);
    PowerInfo info = {0};
    power_get_info(power, &info);
    furi_record_close(RECORD_POWER);
    mjs_return(mjs, mjs_mk_number(mjs, info.charge));
}

void* js_poison_device_create(struct mjs* mjs, mjs_val_t* object, JsModules* modules) {
    if(!js_modules_is_managed(modules)) return NULL;
    mjs_val_t sdk_version = mjs_mk_array(mjs);
    mjs_array_push(mjs, sdk_version, mjs_mk_number(mjs, JS_SDK_MAJOR));
    mjs_array_push(mjs, sdk_version, mjs_mk_number(mjs, JS_SDK_MINOR));
    *object = mjs_mk_object(mjs);
    JS_ASSIGN_MULTI(mjs, *object) {
        JS_FIELD("getModel", MJS_MK_FN(js_poison_device_get_model));
        JS_FIELD("getName", MJS_MK_FN(js_poison_device_get_name));
        JS_FIELD("getBatteryCharge", MJS_MK_FN(js_poison_device_get_battery));
        JS_FIELD("firmwareVendor", mjs_mk_string(mjs, "poisonedos", ~0, false));
        JS_FIELD("jsSdkVersion", sdk_version);
    }
    return modules;
}
