#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <infrared_signal.h>
#include <infrared_worker.h>

typedef struct {
    InfraredWorker* worker;
    InfraredSignal* signal;
    FuriSemaphore* received;
    bool running;
    bool transmitting;
    bool capture_valid;
} PoisonInfraredHandle;

typedef struct {
    bool decoded;
    InfraredProtocol protocol;
    uint32_t address;
    uint32_t command;
    bool repeat;
    size_t timings;
    uint32_t raw_timings[MAX_TIMINGS_AMOUNT];
    uint32_t frequency;
    float duty_cycle;
} PoisonInfraredResult;

#ifdef __cplusplus
extern "C" {
#endif

PoisonInfraredHandle* poison_infrared_open(void);
void poison_infrared_close(PoisonInfraredHandle* handle);
bool poison_infrared_receive(
    PoisonInfraredHandle* handle,
    uint32_t timeout_ms,
    PoisonInfraredResult* result);
bool poison_infrared_transmit(PoisonInfraredHandle* handle);
void poison_infrared_stop(PoisonInfraredHandle* handle);

#ifdef __cplusplus
}
#endif
