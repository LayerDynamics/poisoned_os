#pragma once

#include <stdbool.h>

typedef enum {
    PoisonEspTargetFlipperWifiDevBoard,
    PoisonEspTargetMarauderS2,
    PoisonEspTargetMarauderS3,
    PoisonEspTargetFlipperHttpS2,
    PoisonEspTargetWardriverWroom,
    PoisonEspTargetWardriverS3,
} PoisonEspTarget;

#ifdef __cplusplus
extern "C" {
#endif

bool poison_esp_target_parse(const char* target_id, PoisonEspTarget* target);
const char* poison_esp_target_id(PoisonEspTarget target);

#ifdef __cplusplus
}
#endif
