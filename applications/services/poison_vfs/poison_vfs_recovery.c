#include "poison_vfs_recovery.h"

PoisonVfsRecoveryResult poison_vfs_recover(PoisonVfsJournalRecord* record) {
    if(!record || record->state > PoisonVfsJournalComplete) return PoisonVfsRecoveryRollback;
    if(!record->active) return PoisonVfsRecoveryNoop;
    if(record->state == PoisonVfsJournalMetadataCommitted) {
        record->state = PoisonVfsJournalComplete;
        record->active = false;
        return PoisonVfsRecoveryComplete;
    }
    record->active = false;
    return PoisonVfsRecoveryRollback;
}
