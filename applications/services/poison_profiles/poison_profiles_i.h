#pragma once

#include "poison_profiles.h"

bool poison_profile_store_save(const PoisonProfileStore* store, const char* path);
bool poison_profile_store_load(PoisonProfileStore* store, const char* path);

bool poison_profiles_preview_global(const PoisonProfile* profile, uint64_t role_capability_mask);
bool poison_profiles_apply_global(void);
bool poison_profiles_reset_global(void);
bool poison_profiles_copy_global(
    PoisonProfile* active,
    PoisonProfile* preview,
    bool* preview_valid);
bool poison_profile_assets_available(
    const char* theme_id,
    const char* font_pack_id,
    const char* icon_pack_id,
    const char* menu_id);
