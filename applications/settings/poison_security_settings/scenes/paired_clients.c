#include "paired_clients.h"

void poison_security_paired_clients_show(PoisonSecurityUiState* state) {
    if(state) state->screen = PoisonSecurityScreenPairedClients;
}
