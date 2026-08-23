#include "poison_esp_adapter.h"

#include <stddef.h>
#include <string.h>

bool poison_esp_target_parse(const char* target_id, PoisonEspTarget* target) {
    if(!target_id || !target) return false;
    static const struct {
        const char* id;
        PoisonEspTarget target;
    } targets[] = {
        {"flipper-zero-wifi-dev-board", PoisonEspTargetFlipperWifiDevBoard},
        {"marauder-s2", PoisonEspTargetMarauderS2},
        {"marauder-s3", PoisonEspTargetMarauderS3},
        {"flipperhttp-s2", PoisonEspTargetFlipperHttpS2},
        {"wardriver-wroom", PoisonEspTargetWardriverWroom},
        {"wardriver-s3", PoisonEspTargetWardriverS3},
    };
    for(size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
        if(strcmp(target_id, targets[i].id) == 0) {
            *target = targets[i].target;
            return true;
        }
    }
    return false;
}

const char* poison_esp_target_id(PoisonEspTarget target) {
    static const char* const ids[] = {
        "flipper-zero-wifi-dev-board",
        "marauder-s2",
        "marauder-s3",
        "flipperhttp-s2",
        "wardriver-wroom",
        "wardriver-s3",
    };
    return target < (sizeof(ids) / sizeof(ids[0])) ? ids[target] : NULL;
}
