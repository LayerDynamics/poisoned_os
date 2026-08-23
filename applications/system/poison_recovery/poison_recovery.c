#include "poison_recovery.h"
#include <applications/services/poison_diagnostics/poison_diagnostics.h>
#include <applications/services/poison_startup.h>

#include <string.h>

static PoisonRecovery poison_recovery;

void poison_recovery_boot_init(PoisonRecovery* recovery, bool device_locked, bool last_known_good) {
    if(!recovery) return;
    memset(recovery, 0, sizeof(*recovery));
    recovery->state = PoisonRecoveryIdle;
    recovery->device_locked = device_locked;
    recovery->last_known_good = last_known_good;
    recovery->preserve_user_data = true;
    recovery->operation_sequence = 1u;
}

void poison_recovery_on_system_start(void) {
    if(!poison_startup_is_runtime_boot()) return;
    poison_recovery_boot_init(&poison_recovery, false, true);
}

bool poison_recovery_enter_menu(PoisonRecovery* recovery) {
    if(!recovery || recovery->state != PoisonRecoveryIdle) return false;
    recovery->state = PoisonRecoveryMenu;
    return true;
}

static bool begin(PoisonRecovery* recovery, PoisonRecoveryState state) {
    if(!recovery || recovery->state != PoisonRecoveryMenu || !recovery->last_known_good ||
       !recovery->preserve_user_data)
        return false;
    recovery->state = state;
    if(recovery->operation_sequence != UINT32_MAX) ++recovery->operation_sequence;
    poison_diagnostics_increment(poison_diagnostics_get(), PoisonDiagnosticRecovery);
    return true;
}

bool poison_recovery_begin_firmware(PoisonRecovery* recovery) {
    return begin(recovery, PoisonRecoveryFirmwareRestore);
}
bool poison_recovery_begin_profile(PoisonRecovery* recovery) {
    return begin(recovery, PoisonRecoveryProfileRestore);
}
bool poison_recovery_begin_reindex(PoisonRecovery* recovery) {
    return begin(recovery, PoisonRecoveryStorageReindex);
}

bool poison_recovery_complete(PoisonRecovery* recovery, bool success) {
    if(!recovery || recovery->state < PoisonRecoveryFirmwareRestore ||
       recovery->state > PoisonRecoveryStorageReindex)
        return false;
    recovery->state = success ? PoisonRecoveryComplete : PoisonRecoveryFailed;
    return true;
}

bool poison_recovery_cancel(PoisonRecovery* recovery) {
    if(!recovery || recovery->state < PoisonRecoveryFirmwareRestore ||
       recovery->state > PoisonRecoveryStorageReindex)
        return false;
    recovery->state = PoisonRecoveryCancelled;
    return true;
}
