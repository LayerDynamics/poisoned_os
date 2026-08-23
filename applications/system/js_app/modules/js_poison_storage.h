#pragma once

#include "../js_modules.h"

void* js_poison_storage_create(struct mjs* mjs, mjs_val_t* object, JsModules* modules);
void js_poison_storage_destroy(void* data);
