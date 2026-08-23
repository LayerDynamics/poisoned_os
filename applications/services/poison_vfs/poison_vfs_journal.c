#include "poison_vfs_journal.h"

#include <string.h>

void poison_vfs_journal_init(PoisonVfsJournalRecord* record) {
    if(record) memset(record, 0, sizeof(*record));
}

bool poison_vfs_journal_prepare(
    PoisonVfsJournalRecord* record,
    const char* operation_id,
    const char* path,
    uint64_t expected_size,
    const uint8_t old_digest[32],
    const uint8_t new_digest[32]) {
    if(!record || !operation_id || !path || !old_digest || !new_digest || record->active ||
       strnlen(operation_id, POISON_VFS_OPERATION_ID_MAX + 1u) > POISON_VFS_OPERATION_ID_MAX ||
       !poison_vfs_normalize_path(path, record->path))
        return false;
    size_t operation_length = strlen(operation_id);
    if(operation_length == 0 || operation_length > POISON_VFS_OPERATION_ID_MAX) return false;
    memset(record, 0, sizeof(*record));
    memcpy(record->operation_id, operation_id, operation_length + 1u);
    poison_vfs_normalize_path(path, record->path);
    record->expected_size = expected_size;
    memcpy(record->old_digest, old_digest, 32);
    memcpy(record->new_digest, new_digest, 32);
    record->state = PoisonVfsJournalPrepared;
    record->active = true;
    return true;
}

bool poison_vfs_journal_advance(PoisonVfsJournalRecord* record, PoisonVfsJournalState next_state) {
    if(!record || !record->active || next_state != record->state + 1) return false;
    record->state = next_state;
    if(next_state == PoisonVfsJournalComplete) record->active = false;
    return true;
}

bool poison_vfs_journal_validate(const PoisonVfsJournalRecord* record) {
    if(!record || !record->active || record->state > PoisonVfsJournalComplete ||
       record->operation_id[0] == '\0')
        return false;
    char normalized[POISON_VFS_PATH_MAX + 1u];
    return poison_vfs_normalize_path(record->path, normalized) &&
           strcmp(normalized, record->path) == 0;
}
