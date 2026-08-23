#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool poison_profile_asset_identifier_valid(const char* identifier, size_t max_length);
bool poison_profile_asset_contrast_valid(unsigned int contrast_ratio_x10);

#ifdef __cplusplus
}
#endif
