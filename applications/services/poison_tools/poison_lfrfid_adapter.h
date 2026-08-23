#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POISON_LFRFID_DATA_MAX 64u

typedef struct PoisonLfRfidSession PoisonLfRfidSession;

typedef struct {
    char protocol[33];
    uint8_t data[POISON_LFRFID_DATA_MAX];
    size_t data_size;
} PoisonLfRfidDetection;

PoisonLfRfidSession* poison_lfrfid_session_alloc(void);
bool poison_lfrfid_session_start(PoisonLfRfidSession* session);
bool poison_lfrfid_session_wait(
    PoisonLfRfidSession* session,
    uint32_t timeout_ms,
    PoisonLfRfidDetection* detection);
void poison_lfrfid_session_stop(PoisonLfRfidSession* session);
void poison_lfrfid_session_free(PoisonLfRfidSession* session);

#ifdef __cplusplus
}
#endif
