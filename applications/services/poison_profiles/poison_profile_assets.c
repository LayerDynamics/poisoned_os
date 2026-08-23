#include "poison_profile_assets.h"

#include "../poison_packages/poison_package_manager.h"

#include <string.h>

bool poison_profile_asset_identifier_valid(const char* identifier, size_t max_length) {
    if(!identifier || identifier[0] == '\0' || strnlen(identifier, max_length + 1u) > max_length)
        return false;
    for(const char* cursor = identifier; *cursor; cursor++) {
        if(!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= '0' && *cursor <= '9') ||
             *cursor == '.' || *cursor == '-' || *cursor == '_'))
            return false;
    }
    return true;
}

bool poison_profile_asset_contrast_valid(unsigned int contrast_ratio_x10) {
    return contrast_ratio_x10 >= 45u;
}

static bool poison_profile_asset_is_builtin(const char* identifier) {
    return identifier && strncmp(identifier, "builtin.", 8u) == 0;
}

static bool poison_profile_asset_is_active(
    const PoisonPackageManager* manager,
    const char* identifier,
    const char* primary_type,
    const char* combined_type) {
    return poison_profile_asset_is_builtin(identifier) ||
           poison_package_manager_active_content(manager, identifier, primary_type) ||
           (combined_type &&
            poison_package_manager_active_content(manager, identifier, combined_type)) ||
           poison_package_manager_active_content(manager, identifier, "ui-pack");
}

bool poison_profile_assets_available(
    const char* theme_id,
    const char* font_pack_id,
    const char* icon_pack_id,
    const char* menu_id) {
    const PoisonPackageManager* manager = poison_packages_manager();
    return manager && poison_profile_asset_is_active(manager, theme_id, "theme", NULL) &&
           poison_profile_asset_is_active(manager, font_pack_id, "font", "font-icon") &&
           poison_profile_asset_is_active(manager, icon_pack_id, "icon", "font-icon") &&
           poison_profile_asset_is_active(manager, menu_id, "menu", NULL);
}
