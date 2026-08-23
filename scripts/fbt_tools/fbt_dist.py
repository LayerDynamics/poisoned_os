from SCons.Action import Action
from SCons.Builder import Builder
from SCons.Defaults import Touch


def GetProjetDirName(env, project=None):
    parts = [f"f{env['TARGET_HW']}"]
    if project:
        parts.append(project)
    if env["FIRMWARE_APP_SET"] != "default":
        parts.append(env["FIRMWARE_APP_SET"])

    suffix = ""
    if env["DEBUG"]:
        suffix += "D"
    if env["COMPACT"]:
        suffix += "C"
    if suffix:
        parts.append(suffix)

    return "-".join(parts)


def create_fw_build_targets(env, configuration_name):
    flavor = GetProjetDirName(env, configuration_name)
    build_dir = env.Dir("build").Dir(flavor)
    return env.SConscript(
        "firmware.scons",
        variant_dir=build_dir,
        duplicate=0,
        exports={
            "ENV": env,
            "fw_build_meta": {
                "type": configuration_name,
                "flavor": flavor,
                "build_dir": build_dir,
            },
        },
    )


def AddFwProject(env, base_env, fw_type, fw_env_key):
    project_env = env[fw_env_key] = create_fw_build_targets(base_env, fw_type)
    env.Append(
        DIST_PROJECTS=[
            project_env["FW_FLAVOR"],
        ],
        DIST_DEPENDS=[
            project_env["FW_ARTIFACTS"],
        ],
    )

    env.Replace(DIST_DIR=env.GetProjetDirName())
    return project_env


def AddFwFlashTarget(env, targetenv, **kw):
    fwflash_target = env.FwFlash(
        "#build/flash.flag",
        targetenv["FW_ELF"],
        **kw,
    )
    env.Alias(targetenv.subst("${FIRMWARE_BUILD_CFG}_flash"), fwflash_target)
    if env["FORCE"]:
        env.AlwaysBuild(fwflash_target)
    return fwflash_target


def AddJFlashTarget(env, targetenv, **kw):
    jflash_target = env.JFlash(
        "#build/jflash-${BUILD_CFG}-flash.flag",
        targetenv["FW_BIN"],
        JFLASHADDR=targetenv.subst("$IMAGE_BASE_ADDRESS"),
        BUILD_CFG=targetenv.subst("${FIRMWARE_BUILD_CFG}"),
        **kw,
    )
    env.Alias(targetenv.subst("${FIRMWARE_BUILD_CFG}_jflash"), jflash_target)
    if env["FORCE"]:
        env.AlwaysBuild(jflash_target)
    return jflash_target


def AddUsbFlashTarget(env, file_flag, extra_deps, **kw):
    usb_update = env.UsbInstall(
        file_flag,
        (
            env["DIST_DEPENDS"],
            *extra_deps,
        ),
        **kw,
    )
    if env["FORCE"]:
        env.AlwaysBuild(usb_update)
    return usb_update


def UsbPreflight(env):
    result = env.Execute(
        Action(
            [
                [
                    "${PYTHON3}",
                    "${DEVICE_INSTALL_SCRIPT}",
                    "-p",
                    "${FLIP_PORT}",
                    "--preflight-only",
                ]
            ],
            "${USBPREFLIGHTCOMSTR}",
        )
    )
    if result:
        from SCons.Errors import StopError

        raise StopError("Live Flipper preflight failed; firmware build was not started")


def DistCommand(env, name, source, **kw):
    target = f"dist_{name}"
    command = env.Command(
        target,
        source,
        action=Action(
            [
                [
                    "${PYTHON3}",
                    "${DIST_SCRIPT}",
                    "copy",
                    "-p",
                    "${DIST_PROJECTS}",
                    "-s",
                    "${DIST_SUFFIX}",
                    "${DIST_EXTRA}",
                ]
            ],
            "${DISTCOMSTR}",
        ),
        **kw,
    )
    env.Pseudo(target)
    env.Alias(name, command)
    return command


def generate(env):
    if not env["VERBOSE"]:
        env.SetDefault(
            COPROCOMSTR="\tCOPRO\t${TARGET}",
            DISTCOMSTR="\tDIST\t${TARGET}",
            USBPREFLIGHTCOMSTR="\tPREFLIGHT\tFlipper protobuf RPC",
        )
    env.AddMethod(AddFwProject)
    env.AddMethod(DistCommand)
    env.AddMethod(AddFwFlashTarget)
    env.AddMethod(GetProjetDirName)
    env.AddMethod(AddJFlashTarget)
    env.AddMethod(AddUsbFlashTarget)
    env.AddMethod(UsbPreflight)

    env.SetDefault(
        COPRO_MCU_FAMILY="STM32WB5x",
        SELFUPDATE_SCRIPT="${FBT_SCRIPT_DIR}/selfupdate.py",
        DEVICE_INSTALL_SCRIPT="${FBT_SCRIPT_DIR}/device_install.py",
        DIST_SCRIPT="${FBT_SCRIPT_DIR}/sconsdist.py",
        COPRO_ASSETS_SCRIPT="${FBT_SCRIPT_DIR}/assets.py",
        FW_FLASH_SCRIPT="${FBT_SCRIPT_DIR}/fwflash.py",
        POST_INSTALL_ARGS=[],
    )

    env.Append(
        BUILDERS={
            "FwFlash": Builder(
                action=[
                    [
                        "${PYTHON3}",
                        "${FW_FLASH_SCRIPT}",
                        "-d" if env["VERBOSE"] else "",
                        "--interface=${SWD_TRANSPORT}",
                        "--serial=${SWD_TRANSPORT_SERIAL}",
                        "${SOURCE}",
                        "${ARGS}",
                    ],
                    Touch("${TARGET}"),
                ]
            ),
            "UsbInstall": Builder(
                action=[
                    [
                        "${PYTHON3}",
                        "${DEVICE_INSTALL_SCRIPT}",
                        "-p",
                        "${FLIP_PORT}",
                        "${UPDATE_BUNDLE_DIR}/update.fuf",
                        "${POST_INSTALL_ARGS}",
                        "${ARGS}",
                    ],
                    Touch("${TARGET}"),
                ]
            ),
            "CoproBuilder": Builder(
                action=Action(
                    [
                        [
                            "${PYTHON3}",
                            "${COPRO_ASSETS_SCRIPT}",
                            "copro",
                            "${COPRO_CUBE_DIR}",
                            "${TARGET}",
                            "${COPRO_MCU_FAMILY}",
                            "--cube_ver=${COPRO_CUBE_VERSION}",
                            "--stack_type=${COPRO_STACK_TYPE}",
                            "--stack_file=${COPRO_STACK_BIN}",
                            "--stack_addr=${COPRO_STACK_ADDR}",
                            "${ARGS}",
                        ]
                    ],
                    "${COPROCOMSTR}",
                )
            ),
        }
    )


def exists(env):
    return True
