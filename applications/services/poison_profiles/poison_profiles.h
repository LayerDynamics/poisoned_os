#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_PROFILE_TEXT          65u
#define POISON_PROFILE_LAYOUT_TEXT   257u
#define POISON_PROFILE_DEFAULTS_TEXT 513u
#define POISON_PROFILE_MAX_LIST      32u
#define POISON_PROFILE_KNOWN_GOOD_ID "poisonedos.field-console"
#define POISON_PROFILE_STATE_PATH    "/int/.poison/profile.bin"

typedef struct {
    uint32_t format;
    char id[POISON_PROFILE_TEXT];
    char version[POISON_PROFILE_TEXT];
    char role[POISON_PROFILE_TEXT];
    char enabled_tools[POISON_PROFILE_MAX_LIST][POISON_PROFILE_TEXT];
    size_t enabled_tools_count;
    char favorites[POISON_PROFILE_MAX_LIST][POISON_PROFILE_TEXT];
    size_t favorites_count;
    char hidden_tools[POISON_PROFILE_MAX_LIST][POISON_PROFILE_TEXT];
    size_t hidden_tools_count;
    char shortcuts[POISON_PROFILE_MAX_LIST][POISON_PROFILE_TEXT];
    size_t shortcuts_count;
    char policy_id[POISON_PROFILE_TEXT];
    char theme_id[POISON_PROFILE_TEXT];
    char font_pack_id[POISON_PROFILE_TEXT];
    char icon_pack_id[POISON_PROFILE_TEXT];
    char menu_id[POISON_PROFILE_TEXT];
    char dashboard_layout[POISON_PROFILE_LAYOUT_TEXT];
    char home_presentation[POISON_PROFILE_TEXT];
    char status_presentation[POISON_PROFILE_TEXT];
    char lock_behavior[POISON_PROFILE_TEXT];
    char tool_defaults_json[POISON_PROFILE_DEFAULTS_TEXT];
    char transport_policy[POISON_PROFILE_TEXT];
    char logging_policy[POISON_PROFILE_TEXT];
    char evidence_policy[POISON_PROFILE_TEXT];
    char radio_region[POISON_PROFILE_TEXT];
    char peripheral_safety[POISON_PROFILE_TEXT];
    char classroom_policy[POISON_PROFILE_TEXT];
    uint32_t contrast_ratio_x10;
    uint64_t capability_mask;
    bool notifications_enabled;
    bool haptics_enabled;
    bool classroom_restricted;
    bool known_good;
} PoisonProfile;

typedef struct {
    PoisonProfile active;
    PoisonProfile preview;
    bool preview_valid;
} PoisonProfileStore;

void poison_profiles_on_system_start(void);
void poison_profile_store_init(PoisonProfileStore* store);
bool poison_profile_validate(const PoisonProfile* profile, uint64_t role_capability_mask);
bool poison_profile_preview(
    PoisonProfileStore* store,
    const PoisonProfile* profile,
    uint64_t role_capability_mask);
bool poison_profile_apply(PoisonProfileStore* store);
bool poison_profile_reset(PoisonProfileStore* store);

#ifdef __cplusplus
}
#endif
