#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "poison_vfs_paths.h"

#define POISON_VFS_OPERATION_ID_MAX (64u)

typedef enum {
    PoisonVfsJournalPrepared,
    PoisonVfsJournalDataSynced,
    PoisonVfsJournalMetadataCommitted,
    PoisonVfsJournalComplete,
} PoisonVfsJournalState;

typedef struct {
    bool active;
    char operation_id[POISON_VFS_OPERATION_ID_MAX + 1u];
    char path[POISON_VFS_PATH_MAX + 1u];
    PoisonVfsJournalState state;
    uint64_t expected_size;
    uint8_t old_digest[32];
    uint8_t new_digest[32];
} PoisonVfsJournalRecord;

void poison_vfs_journal_init(PoisonVfsJournalRecord* record);
bool poison_vfs_journal_prepare(
    PoisonVfsJournalRecord* record,
    const char* operation_id,
    const char* path,
    uint64_t expected_size,
    const uint8_t old_digest[32],
    const uint8_t new_digest[32]);
bool poison_vfs_journal_advance(PoisonVfsJournalRecord* record, PoisonVfsJournalState next_state);
bool poison_vfs_journal_validate(const PoisonVfsJournalRecord* record);
