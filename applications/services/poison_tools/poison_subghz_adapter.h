#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <devices/devices.h>
#include <receiver.h>
#include <subghz_worker.h>
#include <level_duration.h>

#define POISON_SUBGHZ_RAW_TIMINGS_MAX (1024u)

typedef struct {
    SubGhzEnvironment* environment;
    SubGhzReceiver* receiver;
    SubGhzWorker* worker;
    const SubGhzDevice* device;
    FuriSemaphore* received;
    FuriString* decoded;
    const LevelDuration* tx_timings;
    uint32_t frequency;
    size_t tx_count;
    size_t tx_index;
    LevelDuration raw_timings[POISON_SUBGHZ_RAW_TIMINGS_MAX];
    size_t raw_count;
    bool raw_overflow;
    bool decoded_ready;
    bool running;
    bool transmitting;
} PoisonSubGhzHandle;

typedef struct {
    uint32_t frequency;
    float rssi;
    uint8_t lqi;
    char decoded[192];
    LevelDuration raw_timings[POISON_SUBGHZ_RAW_TIMINGS_MAX];
    size_t raw_count;
    bool decoded_valid;
    bool raw_overflow;
} PoisonSubGhzResult;

#ifdef __cplusplus
extern "C" {
#endif

PoisonSubGhzHandle* poison_subghz_open(uint32_t frequency);
void poison_subghz_close(PoisonSubGhzHandle* handle);
bool poison_subghz_receive(
    PoisonSubGhzHandle* handle,
    uint32_t timeout_ms,
    PoisonSubGhzResult* result);
bool poison_subghz_frequency_valid(PoisonSubGhzHandle* handle, uint32_t frequency);
void poison_subghz_stop(PoisonSubGhzHandle* handle);
bool poison_subghz_transmit_raw(
    PoisonSubGhzHandle* handle,
    uint32_t frequency,
    const LevelDuration* timings,
    size_t count);
bool poison_subghz_transmit_complete(PoisonSubGhzHandle* handle);

#ifdef __cplusplus
}
#endif
