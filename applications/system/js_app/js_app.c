#include "js_thread.h"
#include "js_thread_i.h"
#include <storage/storage.h>
#include <toolbox/cli/cli_command.h>
#include <cli/cli_main_commands.h>
#include <toolbox/pipe.h>
#include <loader/loader.h>

#define JS_CLI_STACK_SIZE (12u * 1024u)

int32_t js_app(void* arg) {
    Loader* loader = furi_record_open(RECORD_LOADER);
    loader_enqueue_launch(
        loader, EXT_PATH("apps/Scripts/js_runner.fap"), arg, LoaderDeferredLaunchFlagGui);
    furi_record_close(RECORD_LOADER);
    return 0;
}

typedef struct {
    PipeSide* pipe;
    FuriSemaphore* exit_sem;
} JsCliContext;

static void js_cli_print(JsCliContext* ctx, const char* msg) {
    UNUSED(ctx);
    UNUSED(msg);
    pipe_send(ctx->pipe, msg, strlen(msg));
}

static void js_cli_exit(JsCliContext* ctx) {
    furi_check(furi_semaphore_release(ctx->exit_sem) == FuriStatusOk);
}

static void js_cli_callback(JsThreadEvent event, const char* msg, void* context) {
    JsCliContext* ctx = context;
    switch(event) {
    case JsThreadEventError:
        js_cli_print(ctx, "---- ERROR ----\r\n");
        js_cli_print(ctx, msg);
        js_cli_print(ctx, "\r\n");
        break;
    case JsThreadEventErrorTrace:
        js_cli_print(ctx, "Trace:\r\n");
        js_cli_print(ctx, msg);
        js_cli_print(ctx, "\r\n");

        js_cli_exit(ctx); // Exit when an error occurs
        break;
    case JsThreadEventPrint:
        js_cli_print(ctx, msg);
        js_cli_print(ctx, "\r\n");
        break;
    case JsThreadEventDone:
        js_cli_print(ctx, "Script done!\r\n");

        js_cli_exit(ctx);
        break;
    }
}

void js_cli_execute(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(context);

    const char* command = furi_string_get_cstr(args);
    Storage* storage = furi_record_open(RECORD_STORAGE);

    do {
        if(furi_string_size(args) == 0) {
            printf("Usage:\r\njs <path>\r\njs console\r\njs debug <path>\r\n");
            break;
        }

        if(strcmp(command, "console") == 0) {
            js_thread_cli_console(pipe);
            break;
        }

        const char debug_prefix[] = "debug ";
        if(strncmp(command, debug_prefix, sizeof(debug_prefix) - 1u) == 0) {
            const char* path = command + sizeof(debug_prefix) - 1u;
            while(*path == ' ')
                ++path;
            if(!*path || !storage_file_exists(storage, path)) {
                printf("Can not open file %s\r\n", path);
                break;
            }
            js_thread_cli_debug(pipe, path);
            break;
        }

        if(!storage_file_exists(storage, command)) {
            printf("Can not open file %s\r\n", command);
            break;
        }

        JsCliContext ctx = {.pipe = pipe};
        ctx.exit_sem = furi_semaphore_alloc(1, 0);

        printf("Running script %s, press CTRL+C to stop\r\n", command);
        JsThread* js_thread = js_thread_run(command, js_cli_callback, &ctx);

        while(furi_semaphore_acquire(ctx.exit_sem, 100) != FuriStatusOk) {
            if(cli_is_pipe_broken_or_is_etx_next_char(pipe)) break;
        }

        js_thread_stop(js_thread);
        furi_semaphore_free(ctx.exit_sem);
    } while(false);

    furi_record_close(RECORD_STORAGE);
}

void js_app_on_system_start(void) {
#ifdef SRV_CLI
    CliRegistry* registry = furi_record_open(RECORD_CLI);
    cli_registry_add_command_ex(
        registry, "js", CliCommandFlagDefault, js_cli_execute, NULL, JS_CLI_STACK_SIZE);
    furi_record_close(RECORD_CLI);
#endif
}
