#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PoisonRoleOwner,
    PoisonRoleOperator,
    PoisonRoleInstructor,
    PoisonRoleStudent,
    PoisonRoleObserver,
    PoisonRoleCount,
} PoisonRole;

typedef uint32_t PoisonCapability;

#define POISON_CAPABILITY_STATUS      (1u << 0)
#define POISON_CAPABILITY_CONTROL     (1u << 1)
#define POISON_CAPABILITY_LAUNCH      (1u << 2)
#define POISON_CAPABILITY_FILES       (1u << 3)
#define POISON_CAPABILITY_EVIDENCE    (1u << 4)
#define POISON_CAPABILITY_RADIO       (1u << 5)
#define POISON_CAPABILITY_NATIVE      (1u << 6)
#define POISON_CAPABILITY_DESTRUCTIVE (1u << 7)

typedef struct {
    PoisonCapability granted;
    bool allowed;
    uint32_t policy_version;
} PoisonPolicyDecision;

PoisonCapability poison_policy_role_capabilities(PoisonRole role);

PoisonPolicyDecision poison_policy_evaluate(
    PoisonRole role,
    PoisonCapability requested,
    bool device_locked,
    bool physical_confirmation,
    uint32_t policy_version);

#ifdef __cplusplus
}
#endif
