#include "poison_serial_adapter.h"

#include <expansion/expansion.h>
#include <furi.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <string.h>

#define POISON_SERIAL_RX_CAPACITY 2048u
#define POISON_SERIAL_TX_MAX      1024u

struct PoisonSerialSession {
    FuriHalSerialId serial_id;
    uint32_t baudrate;
    FuriHalSerialDataBits data_bits;
    FuriHalSerialParity parity;
    FuriHalSerialStopBits stop_bits;
    FuriHalSerialHandle* handle;
    FuriStreamBuffer* receive;
    uint32_t dropped;
    bool expansion_disabled;
    bool started;
};

static void poison_serial_rx_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    PoisonSerialSession* session = context;
    if(!session || !(event & FuriHalSerialRxEventData)) return;
    const uint8_t byte = furi_hal_serial_async_rx(handle);
    if(furi_stream_buffer_send(session->receive, &byte, 1u, 0u) != 1u) session->dropped++;
}

PoisonSerialSession* poison_serial_session_alloc(
    FuriHalSerialId serial_id,
    uint32_t baudrate,
    FuriHalSerialDataBits data_bits,
    FuriHalSerialParity parity,
    FuriHalSerialStopBits stop_bits) {
    if(serial_id >= FuriHalSerialIdMax || baudrate < 1200u || baudrate > 2000000u ||
       data_bits > FuriHalSerialDataBits9 || parity > FuriHalSerialParityOdd ||
       stop_bits > FuriHalSerialStopBits2) {
        return NULL;
    }
    PoisonSerialSession* session = malloc(sizeof(*session));
    if(!session) return NULL;
    memset(session, 0, sizeof(*session));
    session->serial_id = serial_id;
    session->baudrate = baudrate;
    session->data_bits = data_bits;
    session->parity = parity;
    session->stop_bits = stop_bits;
    session->receive = furi_stream_buffer_alloc(POISON_SERIAL_RX_CAPACITY, 1u);
    if(!session->receive) {
        free(session);
        return NULL;
    }
    return session;
}

bool poison_serial_session_start(PoisonSerialSession* session) {
    if(!session || session->started) return false;
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);
    furi_record_close(RECORD_EXPANSION);
    session->expansion_disabled = true;
    session->handle = furi_hal_serial_control_acquire(session->serial_id);
    if(!session->handle) {
        poison_serial_session_stop(session);
        return false;
    }
    furi_hal_serial_init(session->handle, session->baudrate);
    furi_hal_serial_configure_framing(
        session->handle, session->data_bits, session->parity, session->stop_bits);
    furi_hal_serial_async_rx_start(session->handle, poison_serial_rx_callback, session, false);
    session->dropped = 0u;
    session->started = true;
    return true;
}

bool poison_serial_session_write(
    PoisonSerialSession* session,
    const uint8_t* data,
    size_t data_size) {
    if(!session || !session->started || !data || data_size == 0u ||
       data_size > POISON_SERIAL_TX_MAX) {
        return false;
    }
    furi_hal_serial_tx(session->handle, data, data_size);
    furi_hal_serial_tx_wait_complete(session->handle);
    return true;
}

bool poison_serial_session_receive(
    PoisonSerialSession* session,
    uint8_t* output,
    size_t capacity,
    uint32_t timeout_ms,
    PoisonSerialReceiveResult* result) {
    if(!session || !session->started || !output || capacity == 0u ||
       capacity > POISON_SERIAL_RX_CAPACITY || timeout_ms > 60000u || !result) {
        return false;
    }
    memset(result, 0, sizeof(*result));
    result->bytes_received =
        furi_stream_buffer_receive(session->receive, output, capacity, timeout_ms);
    while(result->bytes_received < capacity) {
        const size_t more = furi_stream_buffer_receive(
            session->receive,
            output + result->bytes_received,
            capacity - result->bytes_received,
            0u);
        if(more == 0u) break;
        result->bytes_received += more;
    }
    result->bytes_dropped = session->dropped;
    result->truncated = session->dropped != 0u;
    session->dropped = 0u;
    return true;
}

void poison_serial_session_stop(PoisonSerialSession* session) {
    if(!session) return;
    if(session->handle) {
        if(session->started) furi_hal_serial_async_rx_stop(session->handle);
        furi_hal_serial_deinit(session->handle);
        furi_hal_serial_control_release(session->handle);
        session->handle = NULL;
    }
    if(session->expansion_disabled) {
        Expansion* expansion = furi_record_open(RECORD_EXPANSION);
        expansion_enable(expansion);
        furi_record_close(RECORD_EXPANSION);
        session->expansion_disabled = false;
    }
    session->started = false;
}

void poison_serial_session_free(PoisonSerialSession* session) {
    if(!session) return;
    poison_serial_session_stop(session);
    furi_stream_buffer_free(session->receive);
    free(session);
}
