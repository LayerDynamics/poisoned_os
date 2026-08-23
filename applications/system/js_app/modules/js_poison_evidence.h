#pragma once

#include "../js_modules.h"

void* js_poison_evidence_create(struct mjs* mjs, mjs_val_t* object, JsModules* modules);
void js_poison_evidence_destroy(void* data);
