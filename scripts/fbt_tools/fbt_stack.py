from SCons.Action import Action
from SCons.Builder import Builder


def generate(env):
    if not env["VERBOSE"]:
        env.SetDefault(STARTUPSTACKCOMSTR="\tSTACK\t${TARGET}")
    env.SetDefault(STARTUP_STACK_ANALYZER="${FBT_SCRIPT_DIR}/fbt/stack_analyzer.py")
    env.Append(
        BUILDERS={
            "FirmwareStackValidator": Builder(
                action=Action(
                    [
                        [
                            "${PYTHON3}",
                            "${STARTUP_STACK_ANALYZER}",
                            "--elf",
                            "${SOURCE}",
                            "--objdump",
                            "${OBJDUMP}",
                            "--root",
                            "${STARTUP_STACK_ROOT}",
                            "--budget",
                            "${STARTUP_STACK_BUDGET}",
                            "--output",
                            "${TARGET}",
                            "${STARTUP_STACK_HOOK_ARGS}",
                            "${STARTUP_STACK_APP_ARGS}",
                            "--rpc-root",
                            "${RPC_STACK_ROOT}",
                            "--rpc-budget-header",
                            "${RPC_STACK_BUDGET_HEADER}",
                            "--rpc-budget-macro",
                            "${RPC_STACK_BUDGET_MACRO}",
                            "--rpc-nested-handler",
                            "${RPC_STACK_NESTED_HANDLER}",
                            "${RPC_STACK_SOURCE_ARGS}",
                        ]
                    ],
                    "${STARTUPSTACKCOMSTR}",
                )
            )
        }
    )


def exists(env):
    return True
