#include "../test.h"
#include "../../../../services/poison_profiles/poison_profiles.h"
#include "../../../../services/poison_profiles/poison_profiles_i.h"

#include <stdio.h>
#include <string.h>

static PoisonProfile profile(void) {
    PoisonProfile result = {.format = 1, .contrast_ratio_x10 = 45, .capability_mask = 1};
    snprintf(result.id, sizeof(result.id), "field");
    snprintf(result.version, sizeof(result.version), "1.0.0");
    snprintf(result.role, sizeof(result.role), "field");
    snprintf(result.policy_id, sizeof(result.policy_id), "builtin.field");
    snprintf(result.theme_id, sizeof(result.theme_id), "builtin.field");
    snprintf(result.font_pack_id, sizeof(result.font_pack_id), "builtin.default");
    snprintf(result.icon_pack_id, sizeof(result.icon_pack_id), "builtin.default");
    snprintf(result.menu_id, sizeof(result.menu_id), "builtin.field");
    snprintf(result.dashboard_layout, sizeof(result.dashboard_layout), "field-console");
    snprintf(result.home_presentation, sizeof(result.home_presentation), "builtin.field");
    snprintf(result.status_presentation, sizeof(result.status_presentation), "builtin.field");
    snprintf(result.lock_behavior, sizeof(result.lock_behavior), "pin");
    snprintf(result.tool_defaults_json, sizeof(result.tool_defaults_json), "{}");
    snprintf(result.transport_policy, sizeof(result.transport_policy), "local-only");
    snprintf(result.logging_policy, sizeof(result.logging_policy), "metadata");
    snprintf(result.evidence_policy, sizeof(result.evidence_policy), "digest-only");
    snprintf(result.radio_region, sizeof(result.radio_region), "device");
    snprintf(result.peripheral_safety, sizeof(result.peripheral_safety), "guarded");
    snprintf(result.classroom_policy, sizeof(result.classroom_policy), "none");
    result.notifications_enabled = true;
    result.haptics_enabled = true;
    return result;
}

MU_TEST(poison_profile_preview_is_atomic_and_role_bounded) {
    PoisonProfileStore store;
    poison_profile_store_init(&store);
    PoisonProfile candidate = profile();
    mu_check(poison_profile_preview(&store, &candidate, 1));
    mu_check(strcmp(store.active.id, POISON_PROFILE_KNOWN_GOOD_ID) == 0);
    mu_check(poison_profile_apply(&store));
    mu_check(strcmp(store.active.id, "field") == 0);
    candidate.capability_mask = 2;
    mu_check(!poison_profile_preview(&store, &candidate, 1));
}

MU_TEST(poison_profile_rejects_inaccessible_contrast_and_preserves_reset) {
    PoisonProfileStore store;
    poison_profile_store_init(&store);
    PoisonProfile candidate = profile();
    candidate.contrast_ratio_x10 = 44;
    mu_check(!poison_profile_preview(&store, &candidate, 1));
    mu_check(poison_profile_reset(&store));
    mu_check(store.active.known_good);
}

MU_TEST(poison_profile_rejects_weakened_policy_and_invalid_classroom_boundary) {
    PoisonProfileStore store;
    poison_profile_store_init(&store);
    PoisonProfile candidate = profile();
    strcpy(candidate.peripheral_safety, "unrestricted");
    mu_check(!poison_profile_preview(&store, &candidate, 1u));
    candidate = profile();
    candidate.classroom_restricted = true;
    strcpy(candidate.classroom_policy, "student");
    mu_check(!poison_profile_preview(&store, &candidate, 1u));
    strcpy(candidate.transport_policy, "classroom-managed");
    mu_check(poison_profile_preview(&store, &candidate, 1u));
}

MU_TEST(poison_profile_persistence_is_atomic_and_rejects_recovery_id_override) {
    PoisonProfileStore store;
    poison_profile_store_init(&store);
    PoisonProfile candidate = profile();
    strcpy(candidate.id, POISON_PROFILE_KNOWN_GOOD_ID);
    mu_check(!poison_profile_preview(&store, &candidate, 1u));
    candidate = profile();
    mu_check(poison_profile_preview(&store, &candidate, 1u));
    mu_check(poison_profile_apply(&store));

    const char* path = EXT_PATH(".tmp/poison-profile-state.bin");
    mu_check(poison_profile_store_save(&store, path));
    PoisonProfileStore restored;
    poison_profile_store_init(&restored);
    mu_check(poison_profile_store_load(&restored, path));
    mu_check(strcmp(restored.active.id, candidate.id) == 0);
    mu_check(strcmp(restored.active.dashboard_layout, candidate.dashboard_layout) == 0);
    mu_check(!restored.preview_valid);
}

MU_TEST_SUITE(poison_profiles_suite) {
    MU_RUN_TEST(poison_profile_preview_is_atomic_and_role_bounded);
    MU_RUN_TEST(poison_profile_rejects_inaccessible_contrast_and_preserves_reset);
    MU_RUN_TEST(poison_profile_rejects_weakened_policy_and_invalid_classroom_boundary);
    MU_RUN_TEST(poison_profile_persistence_is_atomic_and_rejects_recovery_id_override);
}

void poison_profiles_run_tests(void) {
    MU_RUN_SUITE(poison_profiles_suite);
}
