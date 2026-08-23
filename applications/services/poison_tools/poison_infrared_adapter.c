#include "poison_infrared_adapter.h"

#include <furi.h>
#include <furi_hal_infrared.h>

static void poison_infrared_received(void* context, InfraredWorkerSignal* received) {
    PoisonInfraredHandle* handle = context;
    handle->capture_valid = false;
    if(infrared_worker_signal_is_decoded(received)) {
        infrared_signal_set_message(handle->signal, infrared_worker_get_decoded_signal(received));
        handle->capture_valid = true;
    } else {
        const uint32_t* timings = NULL;
        size_t count = 0;
        infrared_worker_get_raw_signal(received, &timings, &count);
        if(count <= MAX_TIMINGS_AMOUNT) {
            infrared_signal_set_raw_signal(handle->signal, timings, count, 38000u, 0.33f);
            handle->capture_valid = count > 0u;
        }
    }
    furi_semaphore_release(handle->received);
}

static void poison_infrared_sent(void* context) {
    PoisonInfraredHandle* handle = context;
    handle->transmitting = false;
    handle->capture_valid = false;
}

PoisonInfraredHandle* poison_infrared_open(void) {
    PoisonInfraredHandle* handle = malloc(sizeof(PoisonInfraredHandle));
    if(!handle) return NULL;
    handle->worker = infrared_worker_alloc();
    handle->signal = infrared_signal_alloc();
    handle->received = furi_semaphore_alloc(1, 0);
    handle->running = false;
    handle->transmitting = false;
    handle->capture_valid = false;
    if(!handle->worker || !handle->signal || !handle->received) {
        poison_infrared_close(handle);
        return NULL;
    }
    infrared_worker_rx_set_received_signal_callback(
        handle->worker, poison_infrared_received, handle);
    infrared_worker_rx_enable_signal_decoding(handle->worker, true);
    return handle;
}

void poison_infrared_close(PoisonInfraredHandle* handle) {
    if(!handle) return;
    poison_infrared_stop(handle);
    if(handle->received) furi_semaphore_free(handle->received);
    if(handle->signal) infrared_signal_free(handle->signal);
    if(handle->worker) infrared_worker_free(handle->worker);
    free(handle);
}

bool poison_infrared_receive(
    PoisonInfraredHandle* handle,
    uint32_t timeout_ms,
    PoisonInfraredResult* result) {
    if(!handle || !result || timeout_ms == 0u || timeout_ms > 60000u || handle->transmitting)
        return false;
    if(!handle->running) {
        infrared_worker_rx_start(handle->worker);
        handle->running = true;
    }
    if(furi_semaphore_acquire(handle->received, timeout_ms) != FuriStatusOk) return false;
    if(!handle->capture_valid || !infrared_signal_is_valid(handle->signal)) return false;
    handle->capture_valid = false;
    memset(result, 0, sizeof(*result));
    result->decoded = !infrared_signal_is_raw(handle->signal);
    if(result->decoded) {
        const InfraredMessage* message = infrared_signal_get_message(handle->signal);
        result->protocol = message->protocol;
        result->address = message->address;
        result->command = message->command;
        result->repeat = message->repeat;
    } else {
        const InfraredRawSignal* raw = infrared_signal_get_raw_signal(handle->signal);
        if(raw->timings_size == 0u || raw->timings_size > COUNT_OF(result->raw_timings))
            return false;
        result->timings = raw->timings_size;
        memcpy(result->raw_timings, raw->timings, raw->timings_size * sizeof(uint32_t));
        result->frequency = raw->frequency;
        result->duty_cycle = raw->duty_cycle;
    }
    return true;
}

bool poison_infrared_load_result(
    PoisonInfraredHandle* handle,
    const PoisonInfraredResult* result,
    size_t maximum_timings) {
    if(!handle || !result || handle->running || handle->transmitting || maximum_timings == 0u ||
       maximum_timings > MAX_TIMINGS_AMOUNT) {
        return false;
    }
    handle->capture_valid = false;
    if(result->decoded) {
        if(!infrared_is_protocol_valid(result->protocol)) return false;
        const uint8_t address_bits = infrared_get_protocol_address_length(result->protocol);
        const uint8_t command_bits = infrared_get_protocol_command_length(result->protocol);
        if(address_bits == 0u || command_bits == 0u ||
           (address_bits < 32u && result->address >= (1u << address_bits)) ||
           (command_bits < 32u && result->command >= (1u << command_bits))) {
            return false;
        }
        const InfraredMessage message = {
            .protocol = result->protocol,
            .address = result->address,
            .command = result->command,
            .repeat = result->repeat,
        };
        infrared_signal_set_message(handle->signal, &message);
    } else {
        if(result->timings == 0u || result->timings > maximum_timings ||
           result->frequency < INFRARED_MIN_FREQUENCY ||
           result->frequency > INFRARED_MAX_FREQUENCY || result->duty_cycle <= 0.0f ||
           result->duty_cycle > 1.0f) {
            return false;
        }
        for(size_t index = 0u; index < result->timings; ++index) {
            if(result->raw_timings[index] == 0u) return false;
        }
        infrared_signal_set_raw_signal(
            handle->signal,
            result->raw_timings,
            result->timings,
            result->frequency,
            result->duty_cycle);
    }
    handle->capture_valid = infrared_signal_is_valid(handle->signal);
    return handle->capture_valid;
}

bool poison_infrared_transmit_once(PoisonInfraredHandle* handle) {
    if(!handle || !handle->capture_valid || !infrared_signal_is_valid(handle->signal) ||
       handle->running || handle->transmitting) {
        return false;
    }
    infrared_signal_transmit(handle->signal);
    return true;
}

bool poison_infrared_transmit(PoisonInfraredHandle* handle) {
    if(!handle || !handle->capture_valid || !infrared_signal_is_valid(handle->signal) ||
       handle->running || handle->transmitting)
        return false;
    if(infrared_signal_is_raw(handle->signal)) {
        const InfraredRawSignal* raw = infrared_signal_get_raw_signal(handle->signal);
        infrared_worker_set_raw_signal(
            handle->worker, raw->timings, raw->timings_size, raw->frequency, raw->duty_cycle);
    } else {
        infrared_worker_set_decoded_signal(
            handle->worker, infrared_signal_get_message(handle->signal));
    }
    infrared_worker_tx_set_get_signal_callback(
        handle->worker, infrared_worker_tx_get_signal_steady_callback, handle->worker);
    infrared_worker_tx_set_signal_sent_callback(handle->worker, poison_infrared_sent, handle);
    infrared_worker_tx_start(handle->worker);
    handle->transmitting = true;
    return true;
}

void poison_infrared_stop(PoisonInfraredHandle* handle) {
    if(!handle) return;
    if(handle->transmitting) {
        infrared_worker_tx_stop(handle->worker);
        handle->transmitting = false;
    }
    if(handle->running) {
        infrared_worker_rx_stop(handle->worker);
        handle->running = false;
    }
}
