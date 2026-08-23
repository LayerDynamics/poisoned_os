#pragma once

#include "poison_app.h"

bool poison_app_dispatch_command(const PoisonAppCommand* command);
bool poison_app_event_subscribe(PoisonAppEventCallback callback, void* context);
void poison_app_event_unsubscribe(void* context);
