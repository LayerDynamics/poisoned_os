#include "../../services/poison_profiles/poison_profiles.h"

#include <furi.h>

#include <string.h>

static void poison_settings_append_list(
    FuriString* output,
    const char* label,
    const char values[POISON_PROFILE_MAX_LIST][POISON_PROFILE_TEXT],
    size_t count) {
    furi_string_cat_printf(output, "%s:", label);
    if(count == 0u) furi_string_cat(output, " none");
    for(size_t index = 0u; index < count; index++) {
        furi_string_cat_printf(output, "%s%s", index == 0u ? " " : ",", values[index]);
    }
    furi_string_push_back(output, '\n');
}

void poison_settings_describe_profile(const PoisonProfile* profile, FuriString* output) {
    furi_check(profile);
    furi_check(output);
    furi_string_reset(output);
    furi_string_cat_printf(
        output,
        "%s %s\nRole: %s\nPolicy: %s\nTheme: %s\nFont: %s\nIcon: %s\nMenu: %s\n"
        "Dashboard: %s\nHome: %s\nStatus: %s\nLock: %s\nNotifications: %s\n"
        "Haptics: %s\nTransport: %s\nLogging: %s\nEvidence: %s\nRegion: %s\n"
        "Peripheral: %s\nClassroom: %s (%s)\nContrast: %lu.%lu:1\nCaps: %08lX%08lX\n"
        "Tool defaults: %s\n",
        profile->id,
        profile->version,
        profile->role,
        profile->policy_id,
        profile->theme_id,
        profile->font_pack_id,
        profile->icon_pack_id,
        profile->menu_id,
        profile->dashboard_layout,
        profile->home_presentation,
        profile->status_presentation,
        profile->lock_behavior,
        profile->notifications_enabled ? "on" : "off",
        profile->haptics_enabled ? "on" : "off",
        profile->transport_policy,
        profile->logging_policy,
        profile->evidence_policy,
        profile->radio_region,
        profile->peripheral_safety,
        profile->classroom_policy,
        profile->classroom_restricted ? "restricted" : "open",
        (unsigned long)(profile->contrast_ratio_x10 / 10u),
        (unsigned long)(profile->contrast_ratio_x10 % 10u),
        (unsigned long)(profile->capability_mask >> 32u),
        (unsigned long)profile->capability_mask,
        profile->tool_defaults_json);
    poison_settings_append_list(
        output, "Enabled", profile->enabled_tools, profile->enabled_tools_count);
    poison_settings_append_list(
        output, "Hidden", profile->hidden_tools, profile->hidden_tools_count);
    poison_settings_append_list(output, "Favorites", profile->favorites, profile->favorites_count);
    poison_settings_append_list(output, "Shortcuts", profile->shortcuts, profile->shortcuts_count);
}

static void poison_settings_changed(FuriString* output, const char* label) {
    furi_string_cat_printf(output, "- %s\n", label);
}

void poison_settings_describe_changes(
    const PoisonProfile* before,
    const PoisonProfile* after,
    FuriString* output) {
    furi_check(before);
    furi_check(after);
    furi_check(output);
    furi_string_set(output, "PREVIEW CONSEQUENCES\n");
#define POISON_CHANGED_TEXT(field, label) \
    if(strcmp(before->field, after->field) != 0) poison_settings_changed(output, label)
#define POISON_CHANGED_VALUE(field, label) \
    if(before->field != after->field) poison_settings_changed(output, label)
    POISON_CHANGED_TEXT(role, "role");
    POISON_CHANGED_TEXT(policy_id, "role policy");
    POISON_CHANGED_VALUE(enabled_tools_count, "enabled tools");
    POISON_CHANGED_VALUE(hidden_tools_count, "hidden tools");
    POISON_CHANGED_VALUE(favorites_count, "favorites");
    POISON_CHANGED_VALUE(shortcuts_count, "shortcuts");
    if(memcmp(before->enabled_tools, after->enabled_tools, sizeof(before->enabled_tools)) != 0)
        poison_settings_changed(output, "enabled tool selection");
    if(memcmp(before->hidden_tools, after->hidden_tools, sizeof(before->hidden_tools)) != 0)
        poison_settings_changed(output, "hidden tool selection");
    if(memcmp(before->favorites, after->favorites, sizeof(before->favorites)) != 0)
        poison_settings_changed(output, "favorite selection");
    if(memcmp(before->shortcuts, after->shortcuts, sizeof(before->shortcuts)) != 0)
        poison_settings_changed(output, "shortcut selection");
    POISON_CHANGED_TEXT(theme_id, "theme package");
    POISON_CHANGED_TEXT(font_pack_id, "font package");
    POISON_CHANGED_TEXT(icon_pack_id, "icon package");
    POISON_CHANGED_TEXT(menu_id, "menu package");
    POISON_CHANGED_TEXT(dashboard_layout, "dashboard layout");
    POISON_CHANGED_TEXT(home_presentation, "home presentation");
    POISON_CHANGED_TEXT(status_presentation, "status presentation");
    POISON_CHANGED_TEXT(lock_behavior, "lock behavior");
    POISON_CHANGED_VALUE(notifications_enabled, "notifications");
    POISON_CHANGED_VALUE(haptics_enabled, "haptics");
    POISON_CHANGED_TEXT(tool_defaults_json, "tool defaults");
    POISON_CHANGED_TEXT(transport_policy, "transport policy");
    POISON_CHANGED_TEXT(logging_policy, "logging policy");
    POISON_CHANGED_TEXT(evidence_policy, "evidence policy");
    POISON_CHANGED_TEXT(radio_region, "radio region");
    POISON_CHANGED_TEXT(peripheral_safety, "peripheral safety");
    POISON_CHANGED_TEXT(classroom_policy, "classroom policy");
    POISON_CHANGED_VALUE(classroom_restricted, "classroom restriction");
    POISON_CHANGED_VALUE(contrast_ratio_x10, "contrast");
    POISON_CHANGED_VALUE(capability_mask, "capabilities");
#undef POISON_CHANGED_VALUE
#undef POISON_CHANGED_TEXT
    if(furi_string_size(output) == strlen("PREVIEW CONSEQUENCES\n")) {
        furi_string_cat(output, "No changes\n");
    }
}
