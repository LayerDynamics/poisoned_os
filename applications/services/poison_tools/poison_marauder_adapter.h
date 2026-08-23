#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <furi_hal.h>

typedef enum {
    PoisonMarauderCommandInfo,
    PoisonMarauderCommandScanAp,
    PoisonMarauderCommandListAp,
    PoisonMarauderCommandStop,
} PoisonMarauderCommand;

#ifdef __cplusplus
extern "C" {
#endif

bool poison_marauder_command_encode(PoisonMarauderCommand command, char* output, size_t capacity);

typedef struct PoisonMarauderSession PoisonMarauderSession;

PoisonMarauderSession* poison_marauder_session_alloc(FuriHalSerialId serial_id, uint32_t baudrate);
bool poison_marauder_session_start(PoisonMarauderSession* session);
bool poison_marauder_session_send(PoisonMarauderSession* session, PoisonMarauderCommand command);
bool poison_marauder_session_send_command(
    PoisonMarauderSession* session,
    const char* command_id,
    const char* argument);
size_t poison_marauder_session_receive(
    PoisonMarauderSession* session,
    uint8_t* output,
    size_t capacity,
    uint32_t timeout_ms);
uint32_t poison_marauder_session_take_dropped(PoisonMarauderSession* session);
void poison_marauder_session_stop(PoisonMarauderSession* session);
void poison_marauder_session_free(PoisonMarauderSession* session);

#ifdef __cplusplus
}
#endif
