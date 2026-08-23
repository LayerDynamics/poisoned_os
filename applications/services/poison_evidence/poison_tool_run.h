#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PoisonToolRunCreated,
    PoisonToolRunRunning,
    PoisonToolRunCompleted,
    PoisonToolRunFailed,
    PoisonToolRunCancelled
} PoisonToolRunState;

typedef struct {
    bool active;
    char run_id[65];
    PoisonToolRunState state;
} PoisonToolRun;

bool poison_tool_run_start(PoisonToolRun* run, const char* run_id);
bool poison_tool_run_finish(PoisonToolRun* run, PoisonToolRunState result);

#ifdef __cplusplus
}
#endif
