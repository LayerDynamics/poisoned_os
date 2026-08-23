#pragma once

#include <stdint.h>

typedef enum {
    PoisonVfsEventMountLost,
    PoisonVfsEventRecovered,
    PoisonVfsEventJournalFailure
} PoisonVfsEvent;
void poison_vfs_record_event(PoisonVfsEvent event);
uint32_t poison_vfs_event_count(PoisonVfsEvent event);
