#include "poison_profiles.h"

#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include <furi.h>
#include <furi_hal_region.h>
#include <mbedtls/sha256.h>
#include <storage/storage.h>

#include "poison_profiles_i.h"
#include "poison_profile_assets.h"

typedef struct {
    char magic[4];
    uint32_t version;
    PoisonProfile active;
    uint8_t digest[32];
} PoisonProfileState;

#define POISON_PROFILE_STATE_VERSION (2u)

static PoisonProfileStore poison_profiles_store_instance;
static FuriMutex* poison_profiles_mutex;

static bool text(const char* value) {
    return value && value[0] != '\0' && strnlen(value, POISON_PROFILE_TEXT) < POISON_PROFILE_TEXT;
}

static bool profile_list_valid(
    const char values[POISON_PROFILE_MAX_LIST][POISON_PROFILE_TEXT],
    size_t count) {
    if(count > POISON_PROFILE_MAX_LIST) return false;
    for(size_t index = 0u; index < count; index++) {
        if(!text(values[index])) return false;
    }
    return true;
}

static bool profile_choice(const char* value, const char* const* choices, size_t choice_count) {
    if(!text(value)) return false;
    for(size_t index = 0u; index < choice_count; index++) {
        if(strcmp(value, choices[index]) == 0) return true;
    }
    return false;
}

static bool profile_tool_defaults_valid(const char* value) {
    if(!value) return false;
    const size_t length = strnlen(value, POISON_PROFILE_DEFAULTS_TEXT);
    return length >= 2u && length < POISON_PROFILE_DEFAULTS_TEXT && value[0] == '{' &&
           value[length - 1u] == '}';
}

static bool profile_region_valid(const char* value) {
    if(!text(value)) return false;
    if(strcmp(value, "device") == 0) return true;
    const char* current = furi_hal_region_get_name();
    return current && strcmp(value, current) == 0;
}

void poison_profiles_on_system_start(void) {
    poison_profiles_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    poison_profile_store_init(&poison_profiles_store_instance);
    (void)poison_profile_store_load(&poison_profiles_store_instance, POISON_PROFILE_STATE_PATH);
}

void poison_profile_store_init(PoisonProfileStore* store) {
    if(!store) return;
    memset(store, 0, sizeof(*store));
    store->active.format = 1;
    snprintf(store->active.id, sizeof(store->active.id), "%s", POISON_PROFILE_KNOWN_GOOD_ID);
    snprintf(store->active.version, sizeof(store->active.version), "1.0.0");
    snprintf(store->active.role, sizeof(store->active.role), "field");
    snprintf(store->active.policy_id, sizeof(store->active.policy_id), "builtin.field");
    snprintf(store->active.theme_id, sizeof(store->active.theme_id), "builtin.field-console");
    snprintf(store->active.font_pack_id, sizeof(store->active.font_pack_id), "builtin.default");
    snprintf(store->active.icon_pack_id, sizeof(store->active.icon_pack_id), "builtin.default");
    snprintf(store->active.menu_id, sizeof(store->active.menu_id), "builtin.field-console");
    snprintf(
        store->active.dashboard_layout, sizeof(store->active.dashboard_layout), "field-console");
    snprintf(
        store->active.home_presentation,
        sizeof(store->active.home_presentation),
        "builtin.field-console");
    snprintf(
        store->active.status_presentation,
        sizeof(store->active.status_presentation),
        "builtin.field-console");
    snprintf(store->active.lock_behavior, sizeof(store->active.lock_behavior), "pin");
    snprintf(store->active.tool_defaults_json, sizeof(store->active.tool_defaults_json), "{}");
    snprintf(store->active.transport_policy, sizeof(store->active.transport_policy), "local-only");
    snprintf(store->active.logging_policy, sizeof(store->active.logging_policy), "metadata");
    snprintf(store->active.evidence_policy, sizeof(store->active.evidence_policy), "digest-only");
    snprintf(store->active.radio_region, sizeof(store->active.radio_region), "device");
    snprintf(store->active.peripheral_safety, sizeof(store->active.peripheral_safety), "guarded");
    snprintf(store->active.classroom_policy, sizeof(store->active.classroom_policy), "none");
    store->active.contrast_ratio_x10 = 45;
    store->active.notifications_enabled = true;
    store->active.haptics_enabled = true;
    store->active.known_good = true;
}

bool poison_profile_validate(const PoisonProfile* profile, uint64_t role_capability_mask) {
    static const char* const lock_choices[] = {"pin", "locked", "classroom"};
    static const char* const transport_choices[] = {"usb-only", "local-only", "classroom-managed"};
    static const char* const logging_choices[] = {"metadata", "detailed"};
    static const char* const evidence_choices[] = {"digest-only", "full-local"};
    static const char* const peripheral_choices[] = {"guarded", "strict"};
    static const char* const classroom_choices[] = {"none", "student", "instructor"};
    return profile && profile->format == 1 && text(profile->id) && text(profile->version) &&
           text(profile->role) && text(profile->policy_id) && text(profile->theme_id) &&
           text(profile->font_pack_id) && text(profile->icon_pack_id) && text(profile->menu_id) &&
           poison_profile_assets_available(
               profile->theme_id, profile->font_pack_id, profile->icon_pack_id, profile->menu_id) &&
           profile_list_valid(profile->enabled_tools, profile->enabled_tools_count) &&
           profile_list_valid(profile->favorites, profile->favorites_count) &&
           profile_list_valid(profile->hidden_tools, profile->hidden_tools_count) &&
           profile_list_valid(profile->shortcuts, profile->shortcuts_count) &&
           profile->dashboard_layout[0] != '\0' &&
           strnlen(profile->dashboard_layout, sizeof(profile->dashboard_layout)) <
               sizeof(profile->dashboard_layout) &&
           text(profile->home_presentation) && text(profile->status_presentation) &&
           profile_choice(profile->lock_behavior, lock_choices, COUNT_OF(lock_choices)) &&
           profile_tool_defaults_valid(profile->tool_defaults_json) &&
           profile_choice(
               profile->transport_policy, transport_choices, COUNT_OF(transport_choices)) &&
           profile_choice(profile->logging_policy, logging_choices, COUNT_OF(logging_choices)) &&
           profile_choice(
               profile->evidence_policy, evidence_choices, COUNT_OF(evidence_choices)) &&
           profile_region_valid(profile->radio_region) &&
           profile_choice(
               profile->peripheral_safety, peripheral_choices, COUNT_OF(peripheral_choices)) &&
           profile_choice(
               profile->classroom_policy, classroom_choices, COUNT_OF(classroom_choices)) &&
           (!profile->classroom_restricted ||
            (strcmp(profile->classroom_policy, "none") != 0 &&
             strcmp(profile->transport_policy, "classroom-managed") == 0)) &&
           profile->contrast_ratio_x10 >= 45 &&
           (profile->capability_mask & ~role_capability_mask) == 0 &&
           (profile->known_good == (strcmp(profile->id, POISON_PROFILE_KNOWN_GOOD_ID) == 0));
}

bool poison_profile_preview(
    PoisonProfileStore* store,
    const PoisonProfile* profile,
    uint64_t role_capability_mask) {
    if(!store || !poison_profile_validate(profile, role_capability_mask)) return false;
    store->preview = *profile;
    store->preview_valid = true;
    return true;
}

bool poison_profile_apply(PoisonProfileStore* store) {
    if(!store || !store->preview_valid) return false;
    store->active = store->preview;
    store->preview_valid = false;
    return true;
}

bool poison_profile_reset(PoisonProfileStore* store) {
    if(!store) return false;
    poison_profile_store_init(store);
    return true;
}

static bool poison_profile_state_digest(const PoisonProfileState* state, uint8_t digest[32]) {
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    const bool ok = mbedtls_sha256_starts(&hash, 0) == 0 &&
                    mbedtls_sha256_update(
                        &hash, (const uint8_t*)state, offsetof(PoisonProfileState, digest)) == 0 &&
                    mbedtls_sha256_finish(&hash, digest) == 0;
    mbedtls_sha256_free(&hash);
    return ok;
}

static bool poison_profile_state_valid(const PoisonProfileState* state) {
    uint8_t digest[32];
    const bool valid = state && memcmp(state->magic, "PPRO", 4u) == 0 &&
                       state->version == POISON_PROFILE_STATE_VERSION &&
                       poison_profile_validate(&state->active, UINT64_MAX) &&
                       poison_profile_state_digest(state, digest) &&
                       memcmp(digest, state->digest, sizeof(digest)) == 0;
    memset(digest, 0, sizeof(digest));
    return valid;
}

bool poison_profile_store_save(const PoisonProfileStore* store, const char* path) {
    if(!store || !path || path[0] != '/' || strstr(path, "..")) return false;
    PoisonProfileState state = {0};
    memcpy(state.magic, "PPRO", 4u);
    state.version = POISON_PROFILE_STATE_VERSION;
    state.active = store->active;
    if(!poison_profile_validate(&state.active, UINT64_MAX) ||
       !poison_profile_state_digest(&state, state.digest)) {
        return false;
    }
    char partial[128];
    if(snprintf(partial, sizeof(partial), "%s.partial", path) >= (int)sizeof(partial))
        return false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = storage_simply_mkdir(storage, "/int/.poison");
    File* file = storage_file_alloc(storage);
    if(ok) ok = storage_file_open(file, partial, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) ok = storage_file_write(file, &state, sizeof(state)) == sizeof(state);
    if(ok) ok = storage_file_sync(file);
    if(storage_file_is_open(file)) ok = storage_file_close(file) && ok;
    if(ok) ok = storage_common_rename(storage, partial, path) == FSE_OK;
    storage_file_free(file);
    if(!ok) (void)storage_simply_remove(storage, partial);
    furi_record_close(RECORD_STORAGE);
    memset(&state, 0, sizeof(state));
    return ok;
}

bool poison_profile_store_load(PoisonProfileStore* store, const char* path) {
    if(!store || !path || path[0] != '/' || strstr(path, "..")) return false;
    PoisonProfileState state = {0};
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING) &&
              storage_file_size(file) == sizeof(state) &&
              storage_file_read(file, &state, sizeof(state)) == sizeof(state) &&
              !storage_file_get_error(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    ok = ok && poison_profile_state_valid(&state);
    if(ok) {
        store->active = state.active;
        memset(&store->preview, 0, sizeof(store->preview));
        store->preview_valid = false;
    }
    memset(&state, 0, sizeof(state));
    return ok;
}

bool poison_profiles_preview_global(const PoisonProfile* profile, uint64_t role_capability_mask) {
    if(!poison_profiles_mutex) return false;
    furi_check(furi_mutex_acquire(poison_profiles_mutex, FuriWaitForever) == FuriStatusOk);
    const bool previewed =
        poison_profile_preview(&poison_profiles_store_instance, profile, role_capability_mask);
    furi_check(furi_mutex_release(poison_profiles_mutex) == FuriStatusOk);
    return previewed;
}

bool poison_profiles_apply_global(void) {
    if(!poison_profiles_mutex) return false;
    furi_check(furi_mutex_acquire(poison_profiles_mutex, FuriWaitForever) == FuriStatusOk);
    PoisonProfileStore candidate = poison_profiles_store_instance;
    const bool applied = poison_profile_apply(&candidate) &&
                         poison_profile_store_save(&candidate, POISON_PROFILE_STATE_PATH);
    if(applied) poison_profiles_store_instance = candidate;
    memset(&candidate, 0, sizeof(candidate));
    furi_check(furi_mutex_release(poison_profiles_mutex) == FuriStatusOk);
    return applied;
}

bool poison_profiles_reset_global(void) {
    if(!poison_profiles_mutex) return false;
    furi_check(furi_mutex_acquire(poison_profiles_mutex, FuriWaitForever) == FuriStatusOk);
    PoisonProfileStore recovery;
    poison_profile_store_init(&recovery);
    const bool reset = poison_profile_store_save(&recovery, POISON_PROFILE_STATE_PATH);
    if(reset) poison_profiles_store_instance = recovery;
    memset(&recovery, 0, sizeof(recovery));
    furi_check(furi_mutex_release(poison_profiles_mutex) == FuriStatusOk);
    return reset;
}

bool poison_profiles_copy_global(
    PoisonProfile* active,
    PoisonProfile* preview,
    bool* preview_valid) {
    if(!poison_profiles_mutex || !active) return false;
    furi_check(furi_mutex_acquire(poison_profiles_mutex, FuriWaitForever) == FuriStatusOk);
    *active = poison_profiles_store_instance.active;
    if(preview) *preview = poison_profiles_store_instance.preview;
    if(preview_valid) *preview_valid = poison_profiles_store_instance.preview_valid;
    furi_check(furi_mutex_release(poison_profiles_mutex) == FuriStatusOk);
    return true;
}
