#include "pair_confirm.h"

void poison_security_pair_confirm_show(PoisonSecurityUiState* state) {
    if(state && state->pairing_active) state->screen = PoisonSecurityScreenPairConfirm;
}
