#include "poison_subghz_adapter.h"

#include <furi.h>
#include <furi_hal_region.h>
#include <string.h>

extern const SubGhzProtocolRegistry subghz_protocol_registry;

static void poison_subghz_received(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder,
    void* context) {
    UNUSED(receiver);
    PoisonSubGhzHandle* handle = context;
    furi_string_reset(handle->decoded);
    subghz_protocol_decoder_base_get_string(decoder, handle->decoded);
    handle->decoded_ready = true;
}

static void poison_subghz_pair(void* context, bool level, uint32_t duration) {
    PoisonSubGhzHandle* handle = context;
    bool capture_complete = false;
    if(handle->raw_count < POISON_SUBGHZ_RAW_TIMINGS_MAX) {
        handle->raw_timings[handle->raw_count++] = level_duration_make(level, duration);
        if(handle->raw_count == POISON_SUBGHZ_RAW_TIMINGS_MAX) {
            capture_complete = true;
        } else if(duration >= 10000u && handle->raw_count > 1u) {
            capture_complete = true;
        }
    } else {
        handle->raw_overflow = true;
        capture_complete = true;
    }
    subghz_receiver_decode(handle->receiver, level, duration);
    if(handle->decoded_ready) capture_complete = true;
    if(capture_complete) furi_semaphore_release(handle->received);
}

static void poison_subghz_overrun(void* context) {
    PoisonSubGhzHandle* handle = context;
    handle->raw_overflow = true;
    furi_semaphore_release(handle->received);
}

static LevelDuration poison_subghz_tx_yield(void* context) {
    PoisonSubGhzHandle* handle = context;
    if(handle->tx_index >= handle->tx_count) return level_duration_reset();
    return handle->tx_timings[handle->tx_index++];
}

PoisonSubGhzHandle* poison_subghz_open(uint32_t frequency) {
    PoisonSubGhzHandle* handle = malloc(sizeof(PoisonSubGhzHandle));
    if(!handle) return NULL;
    memset(handle, 0, sizeof(PoisonSubGhzHandle));
    handle->environment = subghz_environment_alloc();
    handle->decoded = furi_string_alloc();
    handle->received = furi_semaphore_alloc(1, 0);
    handle->device = subghz_devices_get_by_name("cc1101_int");
    if(!handle->environment || !handle->decoded || !handle->received || !handle->device ||
       !subghz_devices_begin(handle->device) ||
       !subghz_devices_is_frequency_valid(handle->device, frequency)) {
        poison_subghz_close(handle);
        return NULL;
    }
    subghz_environment_set_protocol_registry(handle->environment, &subghz_protocol_registry);
    handle->receiver = subghz_receiver_alloc_init(handle->environment);
    handle->worker = subghz_worker_alloc();
    if(!handle->receiver || !handle->worker) {
        poison_subghz_close(handle);
        return NULL;
    }
    subghz_receiver_set_filter(handle->receiver, SubGhzProtocolFlag_Decodable);
    subghz_receiver_set_rx_callback(handle->receiver, poison_subghz_received, handle);
    subghz_worker_set_pair_callback(handle->worker, poison_subghz_pair);
    subghz_worker_set_overrun_callback(handle->worker, poison_subghz_overrun);
    subghz_worker_set_context(handle->worker, handle);
    subghz_devices_set_frequency(handle->device, frequency);
    handle->frequency = frequency;
    return handle;
}

void poison_subghz_close(PoisonSubGhzHandle* handle) {
    if(!handle) return;
    poison_subghz_stop(handle);
    if(handle->worker) subghz_worker_free(handle->worker);
    if(handle->receiver) subghz_receiver_free(handle->receiver);
    if(handle->device) subghz_devices_end(handle->device);
    if(handle->environment) subghz_environment_free(handle->environment);
    if(handle->received) furi_semaphore_free(handle->received);
    if(handle->decoded) furi_string_free(handle->decoded);
    free(handle);
}

bool poison_subghz_frequency_valid(PoisonSubGhzHandle* handle, uint32_t frequency) {
    return handle && handle->device &&
           subghz_devices_is_frequency_valid(handle->device, frequency);
}

bool poison_subghz_receive(
    PoisonSubGhzHandle* handle,
    uint32_t timeout_ms,
    PoisonSubGhzResult* result) {
    if(!handle || !result || !handle->worker || timeout_ms == 0u || timeout_ms > 60000u)
        return false;
    if(!handle->running) {
        handle->raw_count = 0u;
        handle->raw_overflow = false;
        handle->decoded_ready = false;
        furi_string_reset(handle->decoded);
        subghz_devices_set_rx(handle->device);
        subghz_worker_start(handle->worker);
        handle->running = true;
    }
    if(furi_semaphore_acquire(handle->received, timeout_ms) != FuriStatusOk) return false;
    const float rssi = subghz_devices_get_rssi(handle->device);
    const uint8_t lqi = subghz_devices_get_lqi(handle->device);
    subghz_worker_stop(handle->worker);
    subghz_devices_idle(handle->device);
    handle->running = false;
    memset(result, 0, sizeof(*result));
    result->frequency = handle->frequency;
    result->rssi = rssi;
    result->lqi = lqi;
    result->raw_count = handle->raw_count;
    result->raw_overflow = handle->raw_overflow;
    memcpy(result->raw_timings, handle->raw_timings, handle->raw_count * sizeof(LevelDuration));
    const size_t size = furi_string_size(handle->decoded);
    if(size > 0u && size < sizeof(result->decoded)) {
        memcpy(result->decoded, furi_string_get_cstr(handle->decoded), size + 1u);
        result->decoded_valid = true;
    }
    return result->raw_count > 0u && !result->raw_overflow;
}

void poison_subghz_stop(PoisonSubGhzHandle* handle) {
    if(!handle) return;
    if(handle->tx_timings) {
        subghz_devices_stop_async_tx(handle->device);
        handle->tx_timings = NULL;
        handle->tx_count = 0u;
        handle->tx_index = 0u;
        handle->transmitting = false;
        subghz_devices_idle(handle->device);
    }
    if(handle->running) {
        subghz_worker_stop(handle->worker);
        subghz_devices_idle(handle->device);
        handle->running = false;
    }
}

bool poison_subghz_transmit_complete(PoisonSubGhzHandle* handle) {
    if(!handle || !handle->transmitting || !handle->device) return false;
    if(!subghz_devices_is_async_complete_tx(handle->device)) return false;
    subghz_devices_stop_async_tx(handle->device);
    handle->tx_timings = NULL;
    handle->tx_count = 0u;
    handle->tx_index = 0u;
    handle->transmitting = false;
    subghz_devices_idle(handle->device);
    return true;
}

bool poison_subghz_transmit_raw(
    PoisonSubGhzHandle* handle,
    uint32_t frequency,
    const LevelDuration* timings,
    size_t count) {
    if(!handle || !handle->device || !timings || count == 0u ||
       count > POISON_SUBGHZ_RAW_TIMINGS_MAX ||
       !subghz_devices_is_frequency_valid(handle->device, frequency) || handle->running ||
       handle->transmitting || !furi_hal_region_is_provisioned() ||
       !furi_hal_region_is_frequency_allowed(frequency)) {
        return false;
    }
    for(size_t i = 0; i < count; i++) {
        if(level_duration_is_reset(timings[i]) || level_duration_is_wait(timings[i]) ||
           level_duration_get_duration(timings[i]) == 0u) {
            return false;
        }
    }
    subghz_devices_set_frequency(handle->device, frequency);
    handle->frequency = frequency;
    if(!furi_hal_region_is_frequency_allowed(frequency) ||
       !subghz_devices_set_tx(handle->device)) {
        subghz_devices_idle(handle->device);
        return false;
    }
    handle->tx_timings = timings;
    handle->tx_count = count;
    handle->tx_index = 0u;
    handle->transmitting = true;
    if(!subghz_devices_start_async_tx(handle->device, poison_subghz_tx_yield, handle)) {
        handle->tx_timings = NULL;
        handle->tx_count = 0u;
        handle->tx_index = 0u;
        handle->transmitting = false;
        subghz_devices_idle(handle->device);
        return false;
    }
    return true;
}
