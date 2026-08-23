#include "poison_tool_run.h"

#include <string.h>

bool poison_tool_run_start(PoisonToolRun* run, const char* run_id) {
    if(!run || !run_id || run->active || run_id[0] == '\0' ||
       strnlen(run_id, sizeof(run->run_id)) >= sizeof(run->run_id))
        return false;
    memset(run, 0, sizeof(*run));
    strcpy(run->run_id, run_id);
    run->state = PoisonToolRunRunning;
    run->active = true;
    return true;
}

bool poison_tool_run_finish(PoisonToolRun* run, PoisonToolRunState result) {
    if(!run || !run->active || run->state != PoisonToolRunRunning ||
       (result != PoisonToolRunCompleted && result != PoisonToolRunFailed &&
        result != PoisonToolRunCancelled))
        return false;
    run->state = result;
    return true;
}
