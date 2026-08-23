#include "rpc_i.h"
#include "../poison_tools/poison_tools_i.h"

#include <stdbool.h>
#include <string.h>

typedef struct {
    RpcSession* session;
    char active_run_id[65];
    char active_tool_id[65];
    uint32_t active_command_id;
} RpcPoisonTools;

static bool rpc_poison_tool_cancel(uint32_t command_id, const char* reason, void* context) {
    RpcPoisonTools* rpc_tools = context;
    UNUSED(reason);
    if(!rpc_tools || rpc_tools->active_command_id != command_id ||
       rpc_tools->active_run_id[0] == '\0') {
        return false;
    }
    const bool stopped = poison_tools_run_stop(rpc_tools->active_run_id);
    if(stopped) {
        rpc_tools->active_command_id = 0u;
        rpc_tools->active_run_id[0] = '\0';
        rpc_tools->active_tool_id[0] = '\0';
    }
    return stopped;
}

bool rpc_poison_tool_run_is_bounded(const char* tool_id, const char* run_id) {
    return tool_id && run_id && tool_id[0] != '\0' && run_id[0] != '\0' &&
           strnlen(tool_id, 65u) < 65u && strnlen(run_id, 65u) < 65u;
}

bool rpc_poison_tool_run_request_is_valid(const PB_Poison_ToolRun* input) {
    return input && rpc_poison_tool_run_is_bounded(input->tool_id, input->run_id) &&
           input->case_id[0] != '\0' && strnlen(input->case_id, 65u) < 65u &&
           strcmp(input->tool_version, "builtin") == 0 &&
           (strcmp(input->state, "start") == 0 || strcmp(input->state, "stop") == 0);
}

static void rpc_poison_tool_run_process(const PB_Main* request, void* context) {
    RpcPoisonTools* rpc_tools = context;
    if(!rpc_tools || request->which_content != PB_Main_poison_tool_run_tag || request->has_next ||
       !rpc_session_is_secure_dispatch_active(rpc_tools->session)) {
        rpc_send_and_release_empty(
            rpc_tools ? rpc_tools->session : NULL,
            request->command_id,
            PB_CommandStatus_ERROR_INVALID_PARAMETERS);
        return;
    }
    const PB_Poison_ToolRun* input = &request->content.poison_tool_run;
    uint64_t session_id = 0u;
    PoisonRole role = PoisonRoleCount;
    bool accepted = rpc_poison_tool_run_request_is_valid(input) &&
                    rpc_session_get_secure_identity(rpc_tools->session, &session_id, &role);
    if(accepted && strcmp(input->state, "start") == 0) {
        accepted =
            rpc_tools->active_run_id[0] == '\0' &&
            poison_tools_run_start_for_case(input->tool_id, input->run_id, input->case_id, role);
        if(accepted) {
            strcpy(rpc_tools->active_run_id, input->run_id);
            strcpy(rpc_tools->active_tool_id, input->tool_id);
            rpc_tools->active_command_id = request->command_id;
            accepted = rpc_session_register_cancellable(
                rpc_tools->session, request->command_id, rpc_poison_tool_cancel, rpc_tools);
            if(!accepted) {
                (void)poison_tools_run_stop(input->run_id);
                rpc_tools->active_command_id = 0u;
                rpc_tools->active_run_id[0] = '\0';
                rpc_tools->active_tool_id[0] = '\0';
            }
        }
    } else if(accepted && strcmp(input->state, "stop") == 0) {
        accepted = strcmp(rpc_tools->active_run_id, input->run_id) == 0 &&
                   strcmp(rpc_tools->active_tool_id, input->tool_id) == 0 &&
                   poison_tools_run_stop(input->run_id);
        if(accepted) {
            rpc_session_complete_cancellable(
                rpc_tools->session, rpc_tools->active_command_id, rpc_tools);
            rpc_tools->active_command_id = 0u;
            rpc_tools->active_run_id[0] = '\0';
            rpc_tools->active_tool_id[0] = '\0';
        }
    } else {
        accepted = false;
    }
    rpc_send_and_release_empty(
        rpc_tools->session,
        request->command_id,
        accepted ? PB_CommandStatus_OK : PB_CommandStatus_ERROR_INVALID_PARAMETERS);
}

void* rpc_system_poison_tools_alloc(RpcSession* session) {
    RpcPoisonTools* rpc_tools = malloc(sizeof(*rpc_tools));
    furi_check(rpc_tools);
    memset(rpc_tools, 0, sizeof(*rpc_tools));
    rpc_tools->session = session;
    RpcHandler handler = {
        .message_handler = rpc_poison_tool_run_process,
        .decode_submessage = NULL,
        .context = rpc_tools,
    };
    rpc_add_handler(session, PB_Main_poison_tool_run_tag, &handler);
    return rpc_tools;
}

void rpc_system_poison_tools_free(void* context) {
    RpcPoisonTools* rpc_tools = context;
    if(!rpc_tools) return;
    if(rpc_tools->active_run_id[0]) {
        rpc_session_complete_cancellable(
            rpc_tools->session, rpc_tools->active_command_id, rpc_tools);
        poison_tools_run_stop(rpc_tools->active_run_id);
    }
    memset(rpc_tools, 0, sizeof(*rpc_tools));
    free(rpc_tools);
}
