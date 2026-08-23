#include "poison_infrared_adapter.h"

#include <furi.h>

static void poison_infrared_received(void* context, InfraredWorkerSignal* received) {
    PoisonInfraredHandle* handle = context;
    if(infrared_worker_signal_is_decoded(received)) {
        infrared_signal_set_message(handle->signal, infrared_worker_get_decoded_signal(received));
    } else {
        const uint32_t* timings = NULL;
        size_t count = 0;
        infrared_worker_get_raw_signal(received, &timings, &count);
        if(count <= MAX_TIMINGS_AMOUNT) {
            infrared_signal_set_raw_signal(handle->signal, timings, count, 38000u, 0.33f);
        }
    }
    furi_semaphore_release(handle->received);
}

static void poison_infrared_sent(void* context) {
    PoisonInfraredHandle* handle = context;
    handle->transmitting = false;
}

PoisonInfraredHandle* poison_infrared_open(void) {
    PoisonInfraredHandle* handle = malloc(sizeof(PoisonInfraredHandle));
    if(!handle) return NULL;
    handle->worker = infrared_worker_alloc();
    handle->signal = infrared_signal_alloc();
    handle->received = furi_semaphore_alloc(1, 0);
    handle->running = false;
    handle->transmitting = false;
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
    if(!infrared_signal_is_valid(handle->signal)) return false;
    result->decoded = !infrared_signal_is_raw(handle->signal);
    result->timings = 0;
    result->frequency = 0;
    result->duty_cycle = 0.0f;
    if(!result->decoded) {
        const InfraredRawSignal* raw = infrared_signal_get_raw_signal(handle->signal);
        result->timings = raw->timings_size;
        result->frequency = raw->frequency;
        result->duty_cycle = raw->duty_cycle;
    }
    return true;
}

bool poison_infrared_transmit(PoisonInfraredHandle* handle) {
    if(!handle || !infrared_signal_is_valid(handle->signal) || handle->running ||
       handle->transmitting)
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
