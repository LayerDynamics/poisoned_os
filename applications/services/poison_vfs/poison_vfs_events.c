#include "poison_vfs_events.h"

static uint32_t event_counts[3];

void poison_vfs_record_event(PoisonVfsEvent event) {
    if(event < 3 && event_counts[event] != UINT32_MAX) ++event_counts[event];
}
uint32_t poison_vfs_event_count(PoisonVfsEvent event) {
    return event < 3 ? event_counts[event] : 0;
}
