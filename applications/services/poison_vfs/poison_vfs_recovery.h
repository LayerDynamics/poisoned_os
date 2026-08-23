#pragma once

#include "poison_vfs_journal.h"

typedef enum {
    PoisonVfsRecoveryNoop,
    PoisonVfsRecoveryRollback,
    PoisonVfsRecoveryComplete
} PoisonVfsRecoveryResult;

PoisonVfsRecoveryResult poison_vfs_recover(PoisonVfsJournalRecord* record);
