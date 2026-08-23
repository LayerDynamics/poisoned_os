#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <furi_hal_serial_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PoisonSerialSession PoisonSerialSession;

typedef struct {
    size_t bytes_received;
    uint32_t bytes_dropped;
    bool truncated;
} PoisonSerialReceiveResult;

PoisonSerialSession* poison_serial_session_alloc(
    FuriHalSerialId serial_id,
    uint32_t baudrate,
    FuriHalSerialDataBits data_bits,
    FuriHalSerialParity parity,
    FuriHalSerialStopBits stop_bits);
bool poison_serial_session_start(PoisonSerialSession* session);
bool poison_serial_session_write(
    PoisonSerialSession* session,
    const uint8_t* data,
    size_t data_size);
bool poison_serial_session_receive(
    PoisonSerialSession* session,
    uint8_t* output,
    size_t capacity,
    uint32_t timeout_ms,
    PoisonSerialReceiveResult* result);
void poison_serial_session_stop(PoisonSerialSession* session);
void poison_serial_session_free(PoisonSerialSession* session);

#ifdef __cplusplus
}
#endif
