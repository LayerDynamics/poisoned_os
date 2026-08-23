#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PoisonRecoveryIdle,
    PoisonRecoveryMenu,
    PoisonRecoveryFirmwareRestore,
    PoisonRecoveryProfileRestore,
    PoisonRecoveryStorageReindex,
    PoisonRecoveryComplete,
    PoisonRecoveryFailed,
    PoisonRecoveryCancelled,
} PoisonRecoveryState;

typedef struct {
    PoisonRecoveryState state;
    bool device_locked;
    bool last_known_good;
    bool preserve_user_data;
    uint32_t operation_sequence;
} PoisonRecovery;

void poison_recovery_on_system_start(void);
void poison_recovery_boot_init(PoisonRecovery* recovery, bool device_locked, bool last_known_good);
bool poison_recovery_enter_menu(PoisonRecovery* recovery);
bool poison_recovery_begin_firmware(PoisonRecovery* recovery);
bool poison_recovery_begin_profile(PoisonRecovery* recovery);
bool poison_recovery_begin_reindex(PoisonRecovery* recovery);
bool poison_recovery_complete(PoisonRecovery* recovery, bool success);
bool poison_recovery_cancel(PoisonRecovery* recovery);

#ifdef __cplusplus
}
#endif
