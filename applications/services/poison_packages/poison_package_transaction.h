#pragma once

#include <stdbool.h>
#include "poison_content_update.h"

typedef enum {
    PoisonPackageInstalled,
    PoisonPackageStaged,
    PoisonPackageVerified,
    PoisonPackageActive,
    PoisonPackageDisabled,
    PoisonPackageQuarantined,
    PoisonPackageRemoved
} PoisonPackageState;
typedef struct {
    PoisonPackageState state;
    PoisonPackageState rollback_state;
    bool protected_package;
    bool initialized;
    PoisonContentUpdate content_update;
} PoisonPackageTransaction;

PoisonContentUpdateAdmission poison_package_transaction_begin(
    PoisonPackageTransaction* transaction,
    const PoisonContentUpdateManifest* manifest,
    const PoisonContentUpdateEnvironment* environment,
    PoisonPackageState previous_state,
    bool protected_package);
bool poison_package_receive(PoisonPackageTransaction* transaction, uint32_t bytes);
bool poison_package_verify_payload(PoisonPackageTransaction* transaction, const char* digest);
bool poison_package_activate(PoisonPackageTransaction* transaction, bool exact_confirmation);
bool poison_package_report_health(PoisonPackageTransaction* transaction, bool healthy);
bool poison_package_transition(
    PoisonPackageTransaction* transaction,
    PoisonPackageState next_state);
bool poison_package_rollback(PoisonPackageTransaction* transaction);
