#include "poison_marauder_adapter.h"

#include <applications/drivers/esp32marauder/esp32_marauder_driver.h>
#include <expansion/expansion.h>

#include <string.h>

#include <furi.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>

#define POISON_MARAUDER_RX_CAPACITY 2048u

struct PoisonMarauderSession {
    FuriHalSerialId serial_id;
    uint32_t baudrate;
    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx;
    FuriThreadId owner;
    uint32_t dropped;
    bool expansion_disabled;
    bool started;
};

static void poison_marauder_rx_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    PoisonMarauderSession* session = context;
    if(event != FuriHalSerialRxEventData) return;
    uint8_t byte = furi_hal_serial_async_rx(handle);
    if(furi_stream_buffer_send(session->rx, &byte, 1u, 0u) != 1u) session->dropped++;
    furi_thread_flags_set(session->owner, 1u);
}

bool poison_marauder_command_encode(PoisonMarauderCommand command, char* output, size_t capacity) {
    if(!output || capacity == 0u) return false;
    const char* command_id = NULL;
    switch(command) {
    case PoisonMarauderCommandInfo:
        command_id = "info";
        break;
    case PoisonMarauderCommandScanAp:
        command_id = "scan.all";
        break;
    case PoisonMarauderCommandListAp:
        command_id = "list.ap";
        break;
    case PoisonMarauderCommandStop:
        command_id = "stop";
        break;
    default:
        return false;
    }
    return esp32_marauder_command_format(command_id, NULL, output, capacity);
}

PoisonMarauderSession*
    poison_marauder_session_alloc(FuriHalSerialId serial_id, uint32_t baudrate) {
    if(serial_id >= FuriHalSerialIdMax || baudrate == 0u) return NULL;
    PoisonMarauderSession* session = malloc(sizeof(PoisonMarauderSession));
    if(!session) return NULL;
    session->serial_id = serial_id;
    session->baudrate = baudrate;
    session->serial = NULL;
    session->rx = furi_stream_buffer_alloc(POISON_MARAUDER_RX_CAPACITY, 1u);
    if(!session->rx) {
        free(session);
        return NULL;
    }
    session->owner = furi_thread_get_current_id();
    session->dropped = 0u;
    session->expansion_disabled = false;
    session->started = false;
    return session;
}

bool poison_marauder_session_start(PoisonMarauderSession* session) {
    if(!session || session->started) return false;
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);
    furi_record_close(RECORD_EXPANSION);
    session->expansion_disabled = true;
    session->serial = furi_hal_serial_control_acquire(session->serial_id);
    if(!session->serial) {
        poison_marauder_session_stop(session);
        return false;
    }
    furi_hal_serial_init(session->serial, session->baudrate);
    furi_hal_serial_async_rx_start(session->serial, poison_marauder_rx_callback, session, false);
    session->dropped = 0u;
    session->started = true;
    return true;
}

bool poison_marauder_session_send(PoisonMarauderSession* session, PoisonMarauderCommand command) {
    if(!session || !session->started) return false;
    char encoded[16];
    if(!poison_marauder_command_encode(command, encoded, sizeof(encoded))) return false;
    size_t length = strlen(encoded);
    furi_hal_serial_tx(session->serial, (uint8_t*)encoded, length);
    furi_hal_serial_tx_wait_complete(session->serial);
    return true;
}

bool poison_marauder_session_send_command(
    PoisonMarauderSession* session,
    const char* command_id,
    const char* argument) {
    if(!session || !session->started) return false;
    char encoded[ESP32_MARAUDER_COMMAND_MAX];
    if(!esp32_marauder_command_format(command_id, argument, encoded, sizeof(encoded)))
        return false;
    const size_t length = strlen(encoded);
    furi_hal_serial_tx(session->serial, (const uint8_t*)encoded, length);
    furi_hal_serial_tx_wait_complete(session->serial);
    return true;
}

size_t poison_marauder_session_receive(
    PoisonMarauderSession* session,
    uint8_t* output,
    size_t capacity,
    uint32_t timeout_ms) {
    if(!session || !session->started || !output || capacity == 0u) return 0u;
    size_t received = furi_stream_buffer_receive(session->rx, output, capacity, timeout_ms);
    while(received < capacity) {
        size_t more =
            furi_stream_buffer_receive(session->rx, output + received, capacity - received, 0u);
        if(more == 0u) break;
        received += more;
    }
    return received;
}

uint32_t poison_marauder_session_take_dropped(PoisonMarauderSession* session) {
    if(!session) return 0u;
    const uint32_t dropped = session->dropped;
    session->dropped = 0u;
    return dropped;
}

void poison_marauder_session_stop(PoisonMarauderSession* session) {
    if(!session) return;
    if(session->serial) {
        if(session->started) furi_hal_serial_async_rx_stop(session->serial);
        furi_hal_serial_deinit(session->serial);
        furi_hal_serial_control_release(session->serial);
        session->serial = NULL;
    }
    if(session->expansion_disabled) {
        Expansion* expansion = furi_record_open(RECORD_EXPANSION);
        expansion_enable(expansion);
        furi_record_close(RECORD_EXPANSION);
        session->expansion_disabled = false;
    }
    session->started = false;
}

void poison_marauder_session_free(PoisonMarauderSession* session) {
    if(!session) return;
    poison_marauder_session_stop(session);
    furi_stream_buffer_free(session->rx);
    free(session);
}
