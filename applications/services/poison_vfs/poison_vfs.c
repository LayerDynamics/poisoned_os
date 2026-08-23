#include "poison_vfs.h"

#include <applications/services/poison_startup.h>

void poison_vfs_on_system_start(void) {
    if(!poison_startup_is_runtime_boot()) return;
}
