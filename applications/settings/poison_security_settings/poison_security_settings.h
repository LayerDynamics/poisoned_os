#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <furi.h>
#include <gui/gui.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/submenu.h>
#include <gui/view_dispatcher.h>

#include "../../services/rpc/poison_pairing_store.h"

#define POISON_SECURITY_CODE_MAX        (12u)
#define POISON_SECURITY_FINGERPRINT_MAX (64u)
#define POISON_SECURITY_CLIENT_NAME_MAX (32u)

typedef enum {
    PoisonSecurityScreenMenu,
    PoisonSecurityScreenPairConfirm,
    PoisonSecurityScreenPairedClients,
    PoisonSecurityScreenRevokeConfirm,
} PoisonSecurityScreen;

typedef struct {
    PoisonSecurityScreen screen;
    bool pairing_active;
    uint64_t pairing_expires_at_ms;
    char confirmation_code[POISON_SECURITY_CODE_MAX + 1u];
    char fingerprint[POISON_SECURITY_FINGERPRINT_MAX + 1u];
    char client_name[POISON_SECURITY_CLIENT_NAME_MAX + 1u];
    PoisonRole requested_role;
    size_t selected_record;
} PoisonSecurityUiState;

void poison_security_ui_init(PoisonSecurityUiState* state);

bool poison_security_ui_begin_pairing(
    PoisonSecurityUiState* state,
    const char* confirmation_code,
    const char* fingerprint,
    const char* client_name,
    PoisonRole requested_role,
    uint64_t expires_at_ms);

bool poison_security_ui_confirm_pairing(
    PoisonSecurityUiState* state,
    const char* confirmation_code,
    const char* fingerprint,
    const char* client_name,
    PoisonRole requested_role,
    uint64_t now_ms);

bool poison_security_ui_cancel_pairing(PoisonSecurityUiState* state);
bool poison_security_ui_pairing_expired(const PoisonSecurityUiState* state, uint64_t now_ms);
bool poison_security_ui_select_revoke(PoisonSecurityUiState* state, size_t record_index);
