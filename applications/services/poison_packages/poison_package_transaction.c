#include "poison_package_transaction.h"

#include <string.h>

static bool allowed(PoisonPackageState current, PoisonPackageState next) {
    return ((current == PoisonPackageInstalled || current == PoisonPackageRemoved) &&
            next == PoisonPackageStaged) ||
           (current == PoisonPackageStaged &&
            (next == PoisonPackageVerified || next == PoisonPackageQuarantined)) ||
           (current == PoisonPackageVerified && next == PoisonPackageActive) ||
           (current == PoisonPackageActive &&
            (next == PoisonPackageDisabled || next == PoisonPackageRemoved)) ||
           (current == PoisonPackageDisabled &&
            (next == PoisonPackageActive || next == PoisonPackageRemoved)) ||
           (current == PoisonPackageQuarantined && next == PoisonPackageRemoved);
}

PoisonContentUpdateAdmission poison_package_transaction_begin(
    PoisonPackageTransaction* transaction,
    const PoisonContentUpdateManifest* manifest,
    const PoisonContentUpdateEnvironment* environment,
    PoisonPackageState previous_state,
    bool protected_package) {
    if(!transaction || !manifest || manifest->content_type >= PoisonContentUpdateTypeCount ||
       manifest->content_type == PoisonContentUpdateFirmware ||
       (previous_state != PoisonPackageInstalled && previous_state != PoisonPackageActive &&
        previous_state != PoisonPackageDisabled && previous_state != PoisonPackageRemoved)) {
        return PoisonContentUpdateAdmissionInvalid;
    }
    memset(transaction, 0, sizeof(*transaction));
    PoisonContentUpdateAdmission admission =
        poison_content_update_admit(&transaction->content_update, manifest, environment);
    if(admission != PoisonContentUpdateAdmissionOk) return admission;
    transaction->state = previous_state;
    transaction->rollback_state = previous_state;
    transaction->protected_package = protected_package;
    transaction->initialized = true;
    return PoisonContentUpdateAdmissionOk;
}

bool poison_package_receive(PoisonPackageTransaction* transaction, uint32_t bytes) {
    if(!transaction || !transaction->initialized) return false;
    if(transaction->content_update.state == PoisonContentUpdateDiscovered &&
       !poison_content_update_transition(
           &transaction->content_update, PoisonContentUpdateReceiving)) {
        return false;
    }
    if(!poison_content_update_receive(&transaction->content_update, bytes)) return false;
    if(transaction->content_update.received_bytes == transaction->content_update.content_bytes) {
        if(!poison_content_update_transition(
               &transaction->content_update, PoisonContentUpdateStaged)) {
            return false;
        }
        transaction->state = PoisonPackageStaged;
    }
    return true;
}

bool poison_package_verify_payload(PoisonPackageTransaction* transaction, const char* digest) {
    if(!transaction || !transaction->initialized || transaction->state != PoisonPackageStaged)
        return false;
    const bool verified =
        poison_content_update_verify_payload(&transaction->content_update, digest);
    transaction->state = verified ? PoisonPackageVerified : PoisonPackageQuarantined;
    return verified;
}

bool poison_package_activate(PoisonPackageTransaction* transaction, bool exact_confirmation) {
    if(!transaction || !transaction->initialized || transaction->state != PoisonPackageVerified)
        return false;
    if(transaction->content_update.state == PoisonContentUpdateVerified &&
       !poison_content_update_transition(
           &transaction->content_update, PoisonContentUpdateAwaitingConfirmation)) {
        return false;
    }
    if(transaction->content_update.state != PoisonContentUpdateAwaitingConfirmation ||
       !poison_content_update_confirm(&transaction->content_update, exact_confirmation) ||
       !poison_content_update_transition(
           &transaction->content_update, PoisonContentUpdateActivating)) {
        return false;
    }
    return true;
}

bool poison_package_report_health(PoisonPackageTransaction* transaction, bool healthy) {
    if(!transaction || !transaction->initialized ||
       !poison_content_update_report_health(&transaction->content_update, healthy)) {
        return false;
    }
    transaction->state = healthy ? PoisonPackageActive : transaction->rollback_state;
    return true;
}

bool poison_package_transition(
    PoisonPackageTransaction* transaction,
    PoisonPackageState next_state) {
    if(!transaction || (transaction->protected_package && next_state == PoisonPackageRemoved) ||
       !allowed(transaction->state, next_state) ||
       ((next_state == PoisonPackageStaged || next_state == PoisonPackageVerified ||
         next_state == PoisonPackageActive) &&
        (!transaction->initialized ||
         (next_state == PoisonPackageStaged &&
          transaction->content_update.state != PoisonContentUpdateStaged) ||
         (next_state == PoisonPackageVerified &&
          transaction->content_update.state != PoisonContentUpdateVerified) ||
         (next_state == PoisonPackageActive &&
          transaction->content_update.state != PoisonContentUpdateHealthy))))
        return false;
    transaction->rollback_state = transaction->state;
    transaction->state = next_state;
    return true;
}

bool poison_package_rollback(PoisonPackageTransaction* transaction) {
    if(!transaction || transaction->rollback_state == transaction->state ||
       transaction->state == PoisonPackageRemoved)
        return false;
    if(transaction->initialized &&
       transaction->content_update.state != PoisonContentUpdateRolledBack &&
       !poison_content_update_rollback(&transaction->content_update)) {
        return false;
    }
    transaction->state = transaction->rollback_state;
    return true;
}
