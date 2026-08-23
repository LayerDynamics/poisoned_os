#include "poison_recovery.h"

void poison_recovery_boot_entry(PoisonRecovery* recovery, bool locked, bool last_known_good) {
    poison_recovery_boot_init(recovery, locked, last_known_good);
}
