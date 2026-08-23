#include "revoke_confirm.h"

void poison_security_revoke_confirm_show(PoisonSecurityUiState* state, size_t record_index) {
    (void)poison_security_ui_select_revoke(state, record_index);
}
