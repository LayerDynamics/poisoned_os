#include "poison_policy.h"

static const PoisonCapability role_capabilities[PoisonRoleCount] = {
    [PoisonRoleOwner] = POISON_CAPABILITY_STATUS | POISON_CAPABILITY_CONTROL |
                        POISON_CAPABILITY_LAUNCH | POISON_CAPABILITY_FILES |
                        POISON_CAPABILITY_EVIDENCE | POISON_CAPABILITY_RADIO |
                        POISON_CAPABILITY_NATIVE | POISON_CAPABILITY_DESTRUCTIVE,
    [PoisonRoleOperator] = POISON_CAPABILITY_STATUS | POISON_CAPABILITY_CONTROL |
                           POISON_CAPABILITY_LAUNCH | POISON_CAPABILITY_FILES |
                           POISON_CAPABILITY_EVIDENCE | POISON_CAPABILITY_RADIO,
    [PoisonRoleInstructor] = POISON_CAPABILITY_STATUS | POISON_CAPABILITY_CONTROL |
                             POISON_CAPABILITY_LAUNCH | POISON_CAPABILITY_FILES |
                             POISON_CAPABILITY_EVIDENCE,
    [PoisonRoleStudent] = POISON_CAPABILITY_STATUS | POISON_CAPABILITY_CONTROL |
                          POISON_CAPABILITY_LAUNCH,
    [PoisonRoleObserver] = POISON_CAPABILITY_STATUS | POISON_CAPABILITY_EVIDENCE,
};

PoisonCapability poison_policy_role_capabilities(PoisonRole role) {
    if(role >= PoisonRoleCount) return 0;
    return role_capabilities[role];
}

PoisonPolicyDecision poison_policy_evaluate(
    PoisonRole role,
    PoisonCapability requested,
    bool device_locked,
    bool physical_confirmation,
    uint32_t policy_version) {
    PoisonCapability granted = poison_policy_role_capabilities(role) & requested;
    if(device_locked) granted &= POISON_CAPABILITY_STATUS | POISON_CAPABILITY_EVIDENCE;
    if(!physical_confirmation) {
        granted &=
            ~(POISON_CAPABILITY_NATIVE | POISON_CAPABILITY_DESTRUCTIVE | POISON_CAPABILITY_RADIO);
    }
    PoisonPolicyDecision decision = {
        .granted = granted,
        .allowed = granted != 0 && (granted & requested) == requested,
        .policy_version = policy_version,
    };
    return decision;
}
