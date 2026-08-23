#include "poison_workload.h"

bool poison_workload_console_stdout(PoisonWorkload* workload, const char* text) {
    return poison_workload_append_console(workload, PoisonWorkloadConsoleStdout, text);
}

bool poison_workload_console_stderr(PoisonWorkload* workload, const char* text) {
    return poison_workload_append_console(workload, PoisonWorkloadConsoleStderr, text);
}

bool poison_workload_console_log(PoisonWorkload* workload, const char* text) {
    return poison_workload_append_console(workload, PoisonWorkloadConsoleLog, text);
}
