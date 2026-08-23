#pragma once

#include "poison_content_update.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_CONTENT_UPDATE_HEALTH_PENDING_PATH  "/int/.poison/content_update.pending"
#define POISON_CONTENT_UPDATE_HEALTH_COMPLETE_PATH "/int/.poison/content_update.complete"

bool poison_content_update_health_arm_at(
    const PoisonContentUpdate* update,
    const char* pending_path);
bool poison_content_update_health_mark_complete_at(
    const char* pending_path,
    const char* complete_path);
bool poison_content_update_health_completed_at(
    const PoisonContentUpdate* update,
    const char* complete_path);
void poison_content_update_health_clear_at(const char* pending_path, const char* complete_path);

#ifdef __cplusplus
}
#endif
