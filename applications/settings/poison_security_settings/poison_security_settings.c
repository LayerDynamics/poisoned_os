#include "poison_security_settings.h"

#include "scenes/pair_confirm.h"
#include "scenes/paired_clients.h"
#include "scenes/revoke_confirm.h"
#include "../../services/poison_audit/poison_audit.h"
#include "../../services/rpc/rpc_poison_crypto.h"

#include <string.h>

static bool poison_security_copy_bounded(char* destination, size_t capacity, const char* source) {
    if(!destination || !source || source[0] == '\0') return false;
    size_t length = strnlen(source, capacity);
    if(length == 0 || length >= capacity) return false;
    memcpy(destination, source, length + 1u);
    return true;
}

void poison_security_ui_init(PoisonSecurityUiState* state) {
    if(!state) return;
    memset(state, 0, sizeof(*state));
    state->screen = PoisonSecurityScreenMenu;
}

bool poison_security_ui_begin_pairing(
    PoisonSecurityUiState* state,
    const char* confirmation_code,
    const char* fingerprint,
    const char* client_name,
    PoisonRole requested_role,
    uint64_t expires_at_ms) {
    if(!state || !confirmation_code || !fingerprint || !client_name || expires_at_ms == 0 ||
       requested_role >= PoisonRoleCount) {
        return false;
    }
    PoisonSecurityUiState next = {0};
    next.screen = PoisonSecurityScreenPairConfirm;
    next.pairing_active = true;
    next.pairing_expires_at_ms = expires_at_ms;
    next.requested_role = requested_role;
    if(!poison_security_copy_bounded(
           next.confirmation_code, sizeof(next.confirmation_code), confirmation_code) ||
       !poison_security_copy_bounded(next.fingerprint, sizeof(next.fingerprint), fingerprint) ||
       !poison_security_copy_bounded(next.client_name, sizeof(next.client_name), client_name)) {
        return false;
    }
    *state = next;
    return true;
}

bool poison_security_ui_pairing_expired(const PoisonSecurityUiState* state, uint64_t now_ms) {
    return !state || !state->pairing_active || now_ms >= state->pairing_expires_at_ms;
}

bool poison_security_ui_confirm_pairing(
    PoisonSecurityUiState* state,
    const char* confirmation_code,
    const char* fingerprint,
    const char* client_name,
    PoisonRole requested_role,
    uint64_t now_ms) {
    if(!state || !state->pairing_active || poison_security_ui_pairing_expired(state, now_ms) ||
       requested_role != state->requested_role || !confirmation_code || !fingerprint ||
       !client_name || strcmp(confirmation_code, state->confirmation_code) != 0 ||
       strcmp(fingerprint, state->fingerprint) != 0 ||
       strcmp(client_name, state->client_name) != 0) {
        return false;
    }
    state->pairing_active = false;
    state->screen = PoisonSecurityScreenMenu;
    return true;
}

bool poison_security_ui_cancel_pairing(PoisonSecurityUiState* state) {
    if(!state || !state->pairing_active) return false;
    state->pairing_active = false;
    state->screen = PoisonSecurityScreenMenu;
    memset(state->confirmation_code, 0, sizeof(state->confirmation_code));
    memset(state->fingerprint, 0, sizeof(state->fingerprint));
    memset(state->client_name, 0, sizeof(state->client_name));
    return true;
}

bool poison_security_ui_select_revoke(PoisonSecurityUiState* state, size_t record_index) {
    if(!state || record_index >= POISON_PAIRING_MAX_CLIENTS) return false;
    state->selected_record = record_index;
    state->screen = PoisonSecurityScreenRevokeConfirm;
    return true;
}

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    DialogEx* dialog;
    PoisonSecurityUiState state;
    PoisonPairingRecord records[POISON_PAIRING_MAX_CLIENTS];
    char client_labels[POISON_PAIRING_MAX_CLIENTS][POISON_PAIRING_NAME_MAX + 24u];
    char revoke_text[POISON_PAIRING_NAME_MAX + 40u];
} PoisonSecuritySettingsApp;

enum {
    PoisonSecurityEventPair = 1u,
    PoisonSecurityEventClients = 2u,
    PoisonSecurityEventRevokeAll = 3u,
    PoisonSecurityEventRecover = 4u,
    PoisonSecurityEventClientBase = 100u,
    PoisonSecurityEventDialogBase = 1000u,
};

static void poison_security_menu_callback(void* context, uint32_t index);

static const char* poison_security_role_name(PoisonRole role) {
    static const char* const names[PoisonRoleCount] = {
        "Owner", "Operator", "Instructor", "Student", "Observer"};
    return role < PoisonRoleCount ? names[role] : "Invalid";
}

static void poison_security_show_menu(PoisonSecuritySettingsApp* app) {
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Device Security");
    submenu_add_item(
        app->submenu, "Pair client", PoisonSecurityEventPair, poison_security_menu_callback, app);
    submenu_add_item(
        app->submenu,
        "Paired clients",
        PoisonSecurityEventClients,
        poison_security_menu_callback,
        app);
    app->state.screen = PoisonSecurityScreenMenu;
    view_dispatcher_switch_to_view(app->view_dispatcher, 0u);
}

static void poison_security_show_clients(PoisonSecuritySettingsApp* app) {
    memset(app->records, 0, sizeof(app->records));
    memset(app->client_labels, 0, sizeof(app->client_labels));
    const bool registry_ready = poison_pairing_registry_init();
    const size_t slots =
        registry_ready ?
            poison_pairing_registry_snapshot(app->records, POISON_PAIRING_MAX_CLIENTS) :
            0u;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Paired clients");
    size_t active = 0u;
    for(size_t index = 0u; index < slots; ++index) {
        if(!app->records[index].active) continue;
        snprintf(
            app->client_labels[index],
            sizeof(app->client_labels[index]),
            "%s [%02x%02x%02x%02x]%s",
            app->records[index].client_name,
            app->records[index].key_digest[0],
            app->records[index].key_digest[1],
            app->records[index].key_digest[2],
            app->records[index].key_digest[3],
            app->records[index].active_sessions > 0u ? " (active)" : "");
        submenu_add_item(
            app->submenu,
            app->client_labels[index],
            PoisonSecurityEventClientBase + index,
            poison_security_menu_callback,
            app);
        ++active;
    }
    if(!registry_ready) {
        submenu_add_item(
            app->submenu, "Pairing data corrupt", 0u, poison_security_menu_callback, app);
        submenu_add_item(
            app->submenu,
            "Reset pairing data",
            PoisonSecurityEventRecover,
            poison_security_menu_callback,
            app);
    } else if(active == 0u) {
        submenu_add_item(
            app->submenu, "No paired clients", 0u, poison_security_menu_callback, app);
    } else {
        submenu_add_item(
            app->submenu,
            "Revoke all",
            PoisonSecurityEventRevokeAll,
            poison_security_menu_callback,
            app);
    }
    poison_security_paired_clients_show(&app->state);
    view_dispatcher_switch_to_view(app->view_dispatcher, 0u);
}

static void
    poison_security_show_revoke_dialog(PoisonSecuritySettingsApp* app, const char* client_name) {
    dialog_ex_reset(app->dialog);
    dialog_ex_set_header(app->dialog, "Revoke client?", 64, 0, AlignCenter, AlignTop);
    dialog_ex_set_text(app->dialog, client_name, 64, 28, AlignCenter, AlignCenter);
    dialog_ex_set_left_button_text(app->dialog, "Cancel");
    dialog_ex_set_right_button_text(app->dialog, "Revoke");
    view_dispatcher_switch_to_view(app->view_dispatcher, 1u);
}

static void poison_security_audit_revocation(const PoisonPairingRecord* record) {
    if(!record || !record->active) return;
    static const uint8_t actor_name[] = "device-local-owner";
    uint8_t actor_digest[POISON_AUDIT_DIGEST_BYTES];
    if(poison_crypto_sha256(actor_name, sizeof(actor_name) - 1u, actor_digest) !=
       PoisonCryptoResultOk) {
        return;
    }
    PoisonAuditEvent event;
    char metadata[POISON_AUDIT_METADATA_MAX + 1u];
    snprintf(metadata, sizeof(metadata), "name=%.32s", record->client_name);
    (void)poison_audit_append(
        poison_audit_get(),
        actor_digest,
        "pairing.revoke",
        "client",
        PoisonAuditDecisionRevoked,
        ((uint64_t)furi_get_tick() * 1000u) / furi_kernel_get_tick_frequency(),
        record->key_digest,
        metadata,
        &event);
    memset(actor_digest, 0, sizeof(actor_digest));
    memset(&event, 0, sizeof(event));
}

static void poison_security_audit_recovery(void) {
    static const uint8_t actor_name[] = "device-local-owner";
    static const uint8_t correlation_name[] = "pairing-registry-recovery";
    uint8_t actor_digest[POISON_AUDIT_DIGEST_BYTES];
    uint8_t correlation_digest[POISON_AUDIT_DIGEST_BYTES];
    if(poison_crypto_sha256(actor_name, sizeof(actor_name) - 1u, actor_digest) !=
           PoisonCryptoResultOk ||
       poison_crypto_sha256(correlation_name, sizeof(correlation_name) - 1u, correlation_digest) !=
           PoisonCryptoResultOk) {
        memset(actor_digest, 0, sizeof(actor_digest));
        memset(correlation_digest, 0, sizeof(correlation_digest));
        return;
    }
    PoisonAuditEvent event;
    (void)poison_audit_append(
        poison_audit_get(),
        actor_digest,
        "pairing.reset",
        "client-registry",
        PoisonAuditDecisionRevoked,
        ((uint64_t)furi_get_tick() * 1000u) / furi_kernel_get_tick_frequency(),
        correlation_digest,
        "result=recovered",
        &event);
    memset(actor_digest, 0, sizeof(actor_digest));
    memset(correlation_digest, 0, sizeof(correlation_digest));
    memset(&event, 0, sizeof(event));
}

static bool poison_security_custom_event_callback(void* context, uint32_t event) {
    PoisonSecuritySettingsApp* app = context;
    if(event == PoisonSecurityEventPair) {
        dialog_ex_reset(app->dialog);
        dialog_ex_set_header(app->dialog, "Pair from dashboard", 64, 0, AlignCenter, AlignTop);
        dialog_ex_set_text(
            app->dialog,
            "Connect by USB or BLE\nand approve the code",
            64,
            28,
            AlignCenter,
            AlignCenter);
        dialog_ex_set_left_button_text(app->dialog, "Back");
        view_dispatcher_switch_to_view(app->view_dispatcher, 1u);
        return true;
    }
    if(event == PoisonSecurityEventClients) {
        poison_security_show_clients(app);
        return true;
    }
    if(event == PoisonSecurityEventRevokeAll) {
        app->state.selected_record = POISON_PAIRING_MAX_CLIENTS;
        app->state.screen = PoisonSecurityScreenRevokeConfirm;
        poison_security_show_revoke_dialog(app, "All paired clients");
        return true;
    }
    if(event == PoisonSecurityEventRecover) {
        app->state.selected_record = POISON_PAIRING_MAX_CLIENTS + 1u;
        app->state.screen = PoisonSecurityScreenRevokeConfirm;
        dialog_ex_reset(app->dialog);
        dialog_ex_set_header(app->dialog, "Reset pairing data?", 64, 0, AlignCenter, AlignTop);
        dialog_ex_set_text(
            app->dialog, "Corrupt records will\nbe replaced", 64, 28, AlignCenter, AlignCenter);
        dialog_ex_set_left_button_text(app->dialog, "Cancel");
        dialog_ex_set_right_button_text(app->dialog, "Reset");
        view_dispatcher_switch_to_view(app->view_dispatcher, 1u);
        return true;
    }
    if(event >= PoisonSecurityEventClientBase &&
       event < PoisonSecurityEventClientBase + POISON_PAIRING_MAX_CLIENTS) {
        const size_t record_index = event - PoisonSecurityEventClientBase;
        if(app->records[record_index].active &&
           poison_security_ui_select_revoke(&app->state, record_index)) {
            const PoisonPairingRecord* record = &app->records[record_index];
            snprintf(
                app->revoke_text,
                sizeof(app->revoke_text),
                "%s\nID %02x%02x%02x%02x%02x%02x%02x%02x\nRole %s",
                record->client_name,
                record->key_digest[0],
                record->key_digest[1],
                record->key_digest[2],
                record->key_digest[3],
                record->key_digest[4],
                record->key_digest[5],
                record->key_digest[6],
                record->key_digest[7],
                poison_security_role_name(record->role));
            poison_security_show_revoke_dialog(app, app->revoke_text);
        }
        return true;
    }
    if(event >= PoisonSecurityEventDialogBase) {
        const DialogExResult result = (DialogExResult)(event - PoisonSecurityEventDialogBase);
        if(result == DialogExResultRight &&
           app->state.screen == PoisonSecurityScreenRevokeConfirm) {
            if(app->state.selected_record == POISON_PAIRING_MAX_CLIENTS + 1u) {
                if(poison_pairing_registry_recover_corrupt()) {
                    poison_security_audit_recovery();
                }
            } else if(app->state.selected_record == POISON_PAIRING_MAX_CLIENTS) {
                if(poison_pairing_registry_revoke_all() > 0u) {
                    for(size_t index = 0u; index < POISON_PAIRING_MAX_CLIENTS; ++index) {
                        poison_security_audit_revocation(&app->records[index]);
                    }
                }
            } else {
                const size_t index = app->state.selected_record;
                if(poison_pairing_registry_revoke(index)) {
                    poison_security_audit_revocation(&app->records[index]);
                }
            }
        }
        poison_security_show_clients(app);
        return true;
    }
    return false;
}

static void poison_security_menu_callback(void* context, uint32_t index) {
    PoisonSecuritySettingsApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void poison_security_dialog_callback(DialogExResult result, void* context) {
    PoisonSecuritySettingsApp* app = context;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, PoisonSecurityEventDialogBase + (uint32_t)result);
}

static bool poison_security_back_callback(void* context) {
    PoisonSecuritySettingsApp* app = context;
    if(app->state.pairing_active) poison_security_ui_cancel_pairing(&app->state);
    if(app->state.screen == PoisonSecurityScreenMenu) {
        view_dispatcher_stop(app->view_dispatcher);
    } else {
        poison_security_show_menu(app);
    }
    return true;
}

int32_t poison_security_settings_app(void* context) {
    UNUSED(context);
    PoisonSecuritySettingsApp app = {0};
    poison_security_ui_init(&app.state);
    app.gui = furi_record_open(RECORD_GUI);
    app.view_dispatcher = view_dispatcher_alloc();
    app.submenu = submenu_alloc();
    app.dialog = dialog_ex_alloc();
    view_dispatcher_set_event_callback_context(app.view_dispatcher, &app);
    view_dispatcher_set_custom_event_callback(
        app.view_dispatcher, poison_security_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app.view_dispatcher, poison_security_back_callback);
    view_dispatcher_add_view(app.view_dispatcher, 0, submenu_get_view(app.submenu));
    view_dispatcher_add_view(app.view_dispatcher, 1, dialog_ex_get_view(app.dialog));
    dialog_ex_set_context(app.dialog, &app);
    dialog_ex_set_result_callback(app.dialog, poison_security_dialog_callback);
    view_dispatcher_attach_to_gui(app.view_dispatcher, app.gui, ViewDispatcherTypeFullscreen);
    poison_security_show_menu(&app);
    view_dispatcher_run(app.view_dispatcher);
    view_dispatcher_remove_view(app.view_dispatcher, 0);
    view_dispatcher_remove_view(app.view_dispatcher, 1);
    submenu_free(app.submenu);
    dialog_ex_free(app.dialog);
    view_dispatcher_free(app.view_dispatcher);
    furi_record_close(RECORD_GUI);
    return 0;
}
