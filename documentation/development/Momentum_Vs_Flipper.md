# Momentum Firmware Versus Official Flipper Zero Firmware

## Executive summary

Both repositories build a complete operating firmware distribution for the Flipper Zero: the handheld platform through which a user reads, emulates, saves, analyzes, and transmits NFC, LF RFID, iButton, infrared, Sub-GHz, GPIO, USB HID, and Bluetooth data. The official repository is the vendor-maintained reference implementation and development baseline; Momentum is a community distribution built on that foundation that deliberately turns the device into a more configurable, app-heavy, protocol-experimental platform.

The shortest accurate description of the difference is:

- **Official Flipper firmware** prioritizes the vendor-supported baseline: a smaller built-in surface, conservative radio policy, the official application/catalog split, upstream documentation, and dedicated hardware-in-the-loop CI workflows.
- **Momentum** keeps most of the same architecture but modifies all layers that materially shape the product: boot, global settings, desktop and GUI, Archive, power and input services, RPC lock behavior, radio HAL, protocol registries, NFC/LF RFID/iButton/infrared/GPIO apps, USB/BLE identity, JavaScript modules, SDK exports, build defaults, packaging, and release automation. It also pins a 244-FAP community app submodule and ships asset packs.

Momentum is therefore **not merely official firmware plus a theme or a folder of apps**. The app bundle is its largest visible addition, but the more consequential distinction is that Momentum changes core firmware behavior and exposes global policy switches—including an explicit Sub-GHz extended-range and region-bypass path that the source labels as an at-your-own-risk feature.

For an owner choosing firmware, the trade is breadth and control versus upstream conservatism and supportability. For a developer, the trade is a substantially larger API/app surface and more integration points versus the official SDK and CI baseline.

## Scope, snapshots, and evidence rules

This report compares the checked-out contents under:

- `do_not_include/flipperzero-firmware` (abbreviated **OFW**)
- `do_not_include/Momentum-Firmware` (abbreviated **MNTM**)

The exact snapshots inspected were:

| Repository | Branch | Commit | Commit date | Subject |
|---|---:|---|---|---|
| OFW | `dev` | `a55e39395ff31bd5fdf3929c70720a7fb76e5968` | 2026-08-17 | NFC DESFire zero-key-application crash fix |
| MNTM | `dev` | `d3f89dfe2ef6b01839201598e9be1590cba80322` | 2026-08-18 | ESP Flasher quick-flash fix and Marauder 1.14.3 bump |

Both worktrees were clean when measured. Momentum's object database does not contain the inspected OFW commit, so this is **not** a `git diff OFW_HEAD..MNTM_HEAD` ancestry analysis. It is a reproducible comparison of the two checked-out trees, their manifests, and their initialized submodules. A future update to either directory can invalidate counts and line numbers.

Claims below use actual code rather than relying on marketing lists. Source references use `OFW:` and `MNTM:` followed by a path and line range. The repositories' own product descriptions agree with the code-derived framing: OFW identifies itself as the official repository (`OFW: ReadMe.md:5-14`), while Momentum calls itself a custom firmware based on OFW and states feature-rich, stable, and customizable goals (`MNTM: ReadMe.md:15-26`).

The core inventory was reproduced with commands equivalent to:

```bash
git -C do_not_include/flipperzero-firmware ls-files
git -C do_not_include/Momentum-Firmware ls-files
find do_not_include/flipperzero-firmware -path '*/.git' -prune -o -type f -print
find do_not_include/Momentum-Firmware -path '*/.git' -prune -o -type f -print
rg '^\s*appid\s*=' -g application.fam
rg '^\s*apptype\s*=\s*FlipperAppType\.' -g application.fam
```

Static source inspection can establish implemented paths, defaults, and build wiring. It cannot establish RF performance, compatibility with every real credential or remote, runtime stability on every hardware revision, or the quality of every third-party FAP. No hardware was flashed as part of this documentation task.

## Quantitative shape of the fork

### Root repository content

| Measure | OFW | MNTM | Interpretation |
|---|---:|---:|---|
| Root-repository tracked paths | 4,283 | 5,388 | MNTM tracks 1,105 more root paths overall. |
| Common tracked path names | 4,069 | 4,069 | The repositories still share a large structural base. |
| Common regular files, identical bytes and mode | 3,120 | 3,120 | Most common files remain exactly inherited. |
| Common regular files, identical bytes but different mode | 132 | 132 | The file content is inherited, but executable-bit metadata differs. |
| Common regular files, changed content | 805 | 805 | The fork also modifies a wide cross-section of that base. |
| Paths only in the respective root repository | 214 | 1,319 | MNTM adds much more than it removes. |
| Physical files visible with initialized submodules | 9,373 | 22,404 | This includes submodule contents and makes the external-app pack visible. |
| F7 API-symbol CSV lines | 4,169 | 4,677 | MNTM exports a materially larger firmware API surface. |

Twelve common tracked entries are Git submodule links rather than regular files. Of the 4,057 common regular files, 3,120 match in content and mode, 132 match in content but differ in mode, and 805 differ in content. Two of those 805 content-changed files also change mode. The exhaustive directory and path manifests are provided in the appendix.

The largest **MNTM-only root-tracked** concentrations are 592 paths under `assets/packs`, 159 under `applications/main`, 116 under `applications/system`, 115 under `assets/icons`, 88 under `lib/subghz`, 66 under `assets/dolphin`, 33 under `lib/nfc`, 27 under `applications/debug`, 22 under `applications/settings`, 14 under `applications/services`, and 10 under `lib/momentum`. The largest OFW-only concentrations are 91 paths under `assets/icons`, 27 under `applications/main`, 26 under `applications/system`, 25 under `assets/dolphin`, and 15 under `documentation/testing`.

Among the 805 changed common regular files, the largest concentrations are `applications/main` (191), `applications/services` (143), `lib/subghz` (128), `assets/dolphin` (47), `lib/nfc` (47), `applications/settings` (43), `applications/system` (42), and `targets/f7` (18). This is direct evidence that Momentum is a cross-layer fork, not an isolated application overlay.

### Application manifests

The initialized snapshots contain:

| Manifest measure | OFW | MNTM |
|---|---:|---:|
| Visible `application.fam` files | 89 | 336 |
| Active `App(...)` declarations | 202 | 544 |
| `EXTERNAL` app declarations | 23 | 269 |
| `PLUGIN` declarations | 98 | 194 |
| `DEBUG` declarations | 25 | 26 |
| `SERVICE` declarations | 12 | 12 |
| `SETTINGS` declarations | 10 | 11 |

These declarations are build graph nodes, not all home-screen icons. A plugin, service, startup hook, metapackage, and user-launchable FAP each count as an app declaration but serve different purposes. External FAPs are built for SD-card distribution and do not all occupy internal firmware flash.

## What remains fundamentally shared

Momentum retains the same high-level Furi/Flipper architecture: STM32WB target code, services and applications selected by `.fam` manifests, the FBT/SCons build, a dynamically exported FAP API, and the same core protocol-library organization. Its default metapackages still assemble `basic_services`, `main_apps`, `system_apps`, and `settings_apps` (`MNTM: fbt_options.py:93-125`).

Most low-level third-party foundations are pinned identically in the inspected checkouts, including FreeRTOS, nanopb, mbedTLS, microtar, STM32WB CMSIS/HAL/coprocessor, heatshrink, mlib, and the USB library. Momentum's `.gitmodules` retains those dependencies while adding its app pack and uzlib (`MNTM: .gitmodules:1-43`). This shared foundation explains why so many common paths are byte-identical and why Momentum can continue consuming upstream changes.

The two root `LICENSE` files are byte-identical (SHA-256 `3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986`) and contain GNU GPL version 3 (`OFW: LICENSE:1-16`). Individual bundled applications or assets can still carry their own notices; root-license identity does not replace a per-component license audit.

## End-user differences at a glance

| Area | Official firmware | Momentum |
|---|---|---|
| Product posture | Vendor baseline and official app-catalog model | Community distribution optimized for on-device customization and breadth |
| Global customization | Standard desktop/system settings | Dedicated Momentum app controlling interface, protocols, and miscellaneous firmware policy |
| Themes | Built-in assets | SD-card asset packs can replace animations, icons, and fonts; bundled packs included |
| Main menu | Standard list/menu behavior | User-editable app list and nine render styles |
| Desktop shortcuts | Fixed upstream behavior | Eight configurable directional press/hold keybind slots |
| Archive/file browser | Standard tabs and operations | Search, optional internal/disk-image tabs, path modes, hidden files, favorites, info, clipboard operations, and broader file types |
| Power/input | Standard controls | Auto-poweroff, charge ceiling, firmware-upgrade reboot option, and configurable button vibration |
| Lock behavior | PIN lock disables USB CLI | Configurable boot lock; USB and BLE RPC denied by default while PIN-locked; optional wipe after ten failed PIN attempts |
| Sub-GHz | Official frequency validation, region-controlled transmit, official active protocol registry | Wider receive/tune ranges, optional default-range and region bypass, changed active registry, advanced manual creation, receive filters/repeater/autosave/RAW decode/GPS additions |
| NFC | 12 protocol enum entries and 27 supported-card parser sources | 15 protocol entries with NTAG4xx, Type 4 Tag, and EMV; 40 parser sources; additional write/key/transaction UI |
| LF RFID/iButton | 24 LF protocol enum entries; upstream iButton set | 26 LF entries, raw emulation and T5577 password tools, plus DS1420 iButton support |
| Infrared | Four universal categories | Ten universal entry points, including LEDs, fans, optical-disc devices, monitors, signs, and arbitrary library files |
| GPIO | USB-UART, manual GPIO, 5 V control | Adds I2C address scanner and SFP inspection in core, plus many GPIO FAPs |
| BadUSB/HID | Bad USB with keyboard layout/unpair; smaller remote set | “Bad KB” with extensive USB/BLE identity controls; expanded USB/BLE remote views |
| Bluetooth beaconing | Standard BT service | Extra-beacon HAL plus persistent FindMy Flipper FAP/startup service |
| JavaScript | Core runner and official plugin set | Runner packaged as FAP with CLI entry plus Sub-GHz, IR, BLE beacon, USB disk, I2C, SPI, and VGM modules |
| App distribution | Small set of in-tree external apps; official catalog is separate | Initialized Momentum-Apps submodule with 244 external FAP declarations |
| Build default | Debug-oriented, non-compact local build | Size-optimized, non-debug build that includes external apps by default |

## Momentum's global settings layer

### One app controls cross-cutting firmware policy

Momentum adds a `Momentum` menu app and includes it in the main-app metapackage (`MNTM: applications/main/application.fam:1-27`). The first screen divides configuration into **Interface**, **Protocols**, and **Misc** (`MNTM: applications/main/momentum_app/scenes/momentum_app_scene_start.c:14-58`). That vocabulary matches what the implementation does: it is a front end over settings consumed by multiple unrelated services and HAL components, not an isolated preferences screen.

The primary settings record is stored internally at `/.momentum_settings.txt` (`MNTM: lib/momentum/settings.h:8-10`). Its struct spans:

- asset pack, animation speed/cycling, animation unlock, and menu style;
- boot lock, failed-PIN format policy, and locked USB/BLE RPC policy;
- lock-screen power/time/seconds/date/status/status-prompt/transparency/animation choices;
- battery icon and status-bar decorations;
- Archive sorting, hidden/internal visibility, displayed path mode, favorite timeout, and marquee text;
- dark mode and RGB-backlight mode;
- Dolphin “butthurt” timer and midnight formatting;
- pop-up overlay behavior;
- separate SPI selections for external CC1101 and nRF24 hardware;
- separate UART selections for ESP and NMEA/GPS hardware;
- filename-prefix placement, shell-color spoofing, and RPC/VGM foreground/background colors.

The authoritative struct is `MNTM: lib/momentum/settings.h:66-107`; valid ranges and persisted key names are `MNTM: lib/momentum/settings.c:83-124`. Defaults include DSi menu style, lock-on-boot enabled, locked RPC disallowed, folders-first enabled, dark/RGB modes disabled, six-hour Dolphin timer, and standard external SPI/UART routing (`MNTM: lib/momentum/settings.c:9-50`). Values are loaded and clamped from FlipperFormat and saved back through the storage service (`MNTM: lib/momentum/settings.c:126-205`).

### Settings become active before most services

Momentum starts storage first, then—on a normal boot with an SD card—runs migrations, initializes name spoofing, loads Momentum settings, and initializes asset packs before starting the remaining services (`MNTM: furi/flipper.c:169-223`). On a later SD-card mount it repeats migrations, reloads the name and settings, frees the prior asset pack, and loads the new one (`MNTM: furi/flipper.c:130-155`). This early wiring is why global GUI and service code can directly consult `momentum_settings`.

The architecture has an important operational consequence: some custom behavior depends on a present and readable SD card, while the main settings file itself is internal. Despite that internal location, the inspected normal-boot path calls `momentum_settings_load()` only inside its SD-ready branch; when the SD card is unavailable it logs that early initialization is being skipped and compiled defaults remain in memory (`MNTM: furi/flipper.c:187-213`). The mount callback also contains explicit TODOs about services that have already consumed the old name/settings (`MNTM: furi/flipper.c:145-154`), so hot-swapping SD cards is not equivalent to a clean reboot for every consumer.

## Interface, themes, desktop, and file management

### Asset packs and dark mode

Momentum adds 592 root-tracked files under `assets/packs`; the snapshot contains bundled `Momentum` and `WatchDogs` packs. The runtime searches a selected pack for `Icons` and `Fonts`, loads animated/static replacement assets, and builds a lookup from original icons to replacements (`MNTM: lib/momentum/asset_packs.c:13-15`, `MNTM: lib/momentum/asset_packs.c:179-219`, `MNTM: lib/momentum/asset_packs.c:240-251`).

This is global rendering behavior. Canvas font selection substitutes pack fonts, icon drawing substitutes pack icons, and dark mode inverts the normal background/foreground model (`MNTM: applications/services/gui/canvas.c:139-166`, `MNTM: applications/services/gui/canvas.c:185-190`, `MNTM: applications/services/gui/canvas.c:450-478`). A broken or oversized pack can therefore affect many screens, not only the desktop animation.

### Main-menu styles and status bar

`MenuStyle` contains nine styles: List, Wii, DSi, PS4, Vertical, C64, Compact, MNTM, and CoverFlow (`MNTM: lib/momentum/settings.h:23-34`). The generic menu renderer switches on that global setting, so the style applies wherever this menu module is used (`MNTM: applications/services/gui/modules/menu.c:112-177` and following cases).

The main-menu editor goes beyond rendering: it can reset the list, select an item, add an app, move it left/right in order, or remove it (`MNTM: applications/main/momentum_app/scenes/momentum_app_scene_interface_mainmenu.c:69-109`, `MNTM: applications/main/momentum_app/scenes/momentum_app_scene_interface_mainmenu.c:121-168`). On apply, Momentum rewrites the persisted main-menu executable list (`MNTM: applications/main/momentum_app/momentum_app.c:16-27`).

Momentum also exposes seven battery icon renderings—off, bar, percentage, inverse percentage, two retro variants, and bar-plus-percentage—and controls the clock, status icons, borders, and background (`MNTM: applications/main/momentum_app/scenes/momentum_app_scene_interface_statusbar.c:16-31`, `MNTM: applications/main/momentum_app/scenes/momentum_app_scene_interface_statusbar.c:65-113`).

### Desktop keybinds

Momentum models two actions (short press and hold) across four directional keys, yielding eight configurable slots (`MNTM: applications/services/desktop/desktop_keybinds.h:8-22`). Defaults are Up=Lock Menu, Down=Archive, Right=Passport, Left=Clock; hold-Right=Device Info and hold-Left=Lock with PIN (`MNTM: applications/services/desktop/desktop_keybinds.c:61-76`). Targets can be built-in actions, arbitrary app/file paths, or destructive administrative actions such as Wipe Device (`MNTM: applications/services/desktop/desktop_keybinds.c:193-227`).

### Archive is closer to a file manager

Momentum's Archive declares tabs/virtual paths for Favorites, credential families, applications, Search, mounted disk images, internal storage, and the SD-card browser, and recognizes additional file families including WAV, magnetic-stripe, cross-remote, Picopass, JavaScript, disk images, and ProtoPirate (`MNTM: applications/main/archive/helpers/archive_browser.h:10-69`). It can launch a default handler for a path (`MNTM: applications/main/archive/scenes/archive_scene_browser.c:204-221`), manage favorites and file info (`MNTM: applications/main/archive/scenes/archive_scene_browser.c:255-305`), and perform copy/move paste operations (`MNTM: applications/main/archive/scenes/archive_scene_browser.c:318-369`).

The Momentum settings UI adds folders-first, hidden-file visibility, an internal-storage tab, four path-display modes, and a favorite-launch timeout (`MNTM: applications/main/momentum_app/scenes/momentum_app_scene_interface_filebrowser.c:10-15`, `MNTM: applications/main/momentum_app/scenes/momentum_app_scene_interface_filebrowser.c:67-127`).

## Screen, input, power, and Dolphin behavior

### Screen and optional RGB hardware

Dark mode and left-handed orientation work through firmware settings. Momentum also integrates an optional RGB-backlight modification: three configurable LCD LEDs, static colors, wave/static rainbow modes, speed, interval, and saturation (`MNTM: applications/main/momentum_app/scenes/momentum_app_scene_misc_screen.c:39-167`, `MNTM: applications/main/momentum_app/scenes/momentum_app_scene_misc_screen.c:169-242`). RGB-only controls are visibly locked when the feature is disabled with the message “Needs RGB Backlight,” making clear that firmware support does not create RGB capability on stock hardware (`MNTM: applications/main/momentum_app/scenes/momentum_app_scene_misc_screen.c:196-242`). The target light HAL diverts backlight writes through the RGB driver only when enabled (`MNTM: targets/f7/furi_hal/furi_hal_light.c:35-56`).

### Button vibration

Momentum adds an `input_settings` record with vibration duration and a press/release trigger mask (`MNTM: applications/services/input/input_settings.h:5-13`). The input service loads that record and emits vibration after configured press/release events (`MNTM: applications/services/input/input.c:86-100`, `MNTM: applications/services/input/input.c:145-166`). This is tactile feedback on every configured input transition, not merely vibration from a single application.

### Power policy

Momentum's power settings add an idle auto-poweroff delay and charge-suppression percentage (`MNTM: applications/services/power/power_service/power_settings.h:6-12`). The UI offers off, 5–90 minutes, and 2–48 hour delays plus charge limits in 5% steps (`MNTM: applications/settings/power_settings_app/scenes/power_settings_scene_start.c:12-63`, `MNTM: applications/settings/power_settings_app/scenes/power_settings_scene_start.c:72-112`). The service actively enters or exits charge suppression at the selected level during periodic power ticks (`MNTM: applications/services/power/power_service/power.c:559-595`). Its reboot submenu explicitly offers firmware-upgrade/DFU and normal reboot modes (`MNTM: applications/settings/power_settings_app/scenes/power_settings_scene_reboot.c:14-47`). No equivalent `auto_poweroff_delay_ms` or `charge_supress_percent` implementation exists in the inspected OFW tree.

### Dolphin progression controls

Momentum allows direct level, XP, and anger edits and exposes its configurable “butthurt timer” from off through 48 hours (`MNTM: applications/main/momentum_app/scenes/momentum_app_scene_misc_dolphin.c:15-77`, `MNTM: applications/main/momentum_app/scenes/momentum_app_scene_misc_dolphin.c:79-110`). This changes the virtual Dolphin progression/personality experience; it does not add RF capability.

## Locking, RPC, and destructive safety options

OFW's PIN lock sets the RTC lock flag and disables the USB CLI when a PIN is configured (`OFW: applications/services/desktop/desktop.c:390-429`). Its RPC session constructor itself does not reject sessions based on lock state (`OFW: applications/services/rpc/rpc.c:385-399`).

Momentum expands this in several directions:

- Lock-on-boot is enabled by default when a PIN is set.
- USB and BLE RPC are both disallowed by default while PIN-locked.
- Locking disables USB CLI and closes BLE RPC; unlocking restores them unless the user has explicitly allowed locked access (`MNTM: applications/services/desktop/desktop.c:391-450`).
- The RPC constructor independently rejects USB/BLE owners while locked unless the corresponding allow setting is enabled (`MNTM: applications/services/rpc/rpc.c:388-392`).
- Lock-screen display, poweroff, prompt, transparency, and animation behavior are configurable (`MNTM: applications/main/momentum_app/scenes/momentum_app_scene_interface_lockscreen.c:122-240`).
- An optional “Format on 10 Bad PINs” setting formats the SD card, resets RTC registers, and reboots after the threshold (`MNTM: applications/services/desktop/scenes/desktop_scene_pin_input.c:53-71`). It is **off by default** (`MNTM: lib/momentum/settings.c:15-18`).
- A separate Wipe Device screen requires five right-button confirmations before resetting RTC state and scheduling internal storage formatting (`MNTM: applications/settings/storage_settings/scenes/storage_settings_scene_wipe_device.c:4-71`).

The default locked-RPC changes are a security tightening relative to the inspected OFW constructor. The optional failed-PIN format and keybind-accessible wipe paths are high-impact features that owners should understand before enabling or assigning them.

## Sub-GHz: the largest core protocol divergence

### Hardware-valid and transmit-allowed are separate questions

OFW accepts three CC1101 tuning ranges: approximately 300–348, 387–464, and 779–928 MHz (`OFW: targets/f7/furi_hal/furi_hal_subghz.c:338-359`). It then sets transmit-versus-receive-only regulation from the provisioned region (`OFW: targets/f7/furi_hal/furi_hal_subghz.c:362-376`).

Momentum widens the hardware-valid ranges to 281–361, 378–481, and 749–962 MHz. The source comment explicitly warns that the PLL may not lock and that device damage has been warned about (`MNTM: targets/f7/furi_hal/furi_hal_subghz.c:371-384`). Momentum then evaluates three gates:

1. Is the frequency inside its extended hardware-valid range?
2. If extended mode is off, is it inside the slightly widened default ranges (up to 350, 467.75, and 928 MHz)?
3. If region bypass is off, is the region provisioned and is the frequency region-allowed?

Those statuses are implemented at `MNTM: targets/f7/furi_hal/furi_hal_subghz.c:401-435`. The settings hook reads `use_ext_range_at_own_risk` and `ignore_default_tx_region` from `/ext/subghz/assets/extend_range.txt` and applies both to the HAL (`MNTM: applications/main/subghz/subghz_extended_freq.c:10-30`).

This distinction matters:

- Wider **receive/tune validity** is not a promise that the stock RF path performs well at the edges.
- Enabling **extended range** bypasses Momentum's default-frequency gate.
- Enabling **ignore default TX region** bypasses regional transmit gating in this code path. It can make transmissions technically possible where they are unlawful. Local radio regulations and hardware risk remain the owner's responsibility.

### Active protocol registry

Counting active, uncommented entries in `lib/subghz/protocols/protocol_items.c` gives 53 OFW protocols and 58 Momentum protocols. Momentum's active registry adds eight names relative to this OFW snapshot:

- Allstar Firefly
- Beninca ARC
- Ditec GOL4
- Honeywell
- Jarolift
- Keyfinder
- Nord Ice
- Treadmill37

It omits three names active in this OFW snapshot:

- Kia
- Scher-Khan
- Star Line

Momentum's active table is visible at `MNTM: lib/subghz/protocols/protocol_items.c:3-95`. Numerous weather, TPMS, POCSAG, X10, and Hormann BiSecur symbols are present nearby but commented out (`MNTM: lib/subghz/protocols/protocol_items.c:43-73`); they must not be counted as active core registry support. Some are instead delivered through specialized external apps. Momentum has 94 C source files in `lib/subghz/protocols` versus OFW's 56, but source presence, registry activation, and a working user workflow are three different levels of support.

### Core Sub-GHz workflow changes

OFW's start menu contains Read, Read RAW, Saved, Add Manually, Frequency Analyzer, Region Information, and Radio Settings (`OFW: applications/main/subghz/scenes/subghz_scene_start.c:19-57`). Momentum replaces Region Information with an **Add Manually [Advanced]** entry and routes both manual-creation entries through `subghz_add_manually.fap` before resuming the main app (`MNTM: applications/main/subghz/scenes/subghz_scene_start.c:13-56`, `MNTM: applications/main/subghz/scenes/subghz_scene_start.c:79-101`).

Momentum's receive configuration adds or exposes binary RAW decoding, repeater mode, duplicate removal, delete-old-on-full, autosave, filters for selected alarm/sensor/protocol classes, sound, keyboard lock, and RAW RSSI threshold (`MNTM: applications/main/subghz/scenes/subghz_scene_receiver_config.c:559-749`). Its RAW decoder streams saved pulse durations back through the protocol receiver and reports progress (`MNTM: applications/main/subghz/scenes/subghz_scene_decode_raw.c:129-208`).

Momentum also integrates an external NMEA receiver into Sub-GHz capture. It parses RMC/GGA/GLL latitude, longitude, time, and satellite data over the globally selected UART (`MNTM: applications/main/subghz/helpers/subghz_gps.c:4-53`), and the UI shows the capture location plus live distance/bearing data when GPS is available (`MNTM: applications/main/subghz/scenes/subghz_scene_show_gps.c:4-54`).

## NFC

### Protocol layer

The OFW `NfcProtocol` enum contains 12 protocols. Momentum retains those and adds:

- `NfcProtocolNtag4xx`
- `NfcProtocolType4Tag`
- `NfcProtocolEmv`

Compare `OFW: lib/nfc/protocols/nfc_protocol.h:178-196` with `MNTM: lib/nfc/protocols/nfc_protocol.h:178-199`. Momentum also registers matching protocol-support plugins (`MNTM: applications/main/nfc/application.fam:178-215`) and maps them into the app's support layer (`MNTM: applications/main/nfc/helpers/protocol_support/nfc_protocol_support.c:104-120`).

### Card parsers

OFW has 27 C parser sources under `applications/main/nfc/plugins/supported_cards`; Momentum has 40. Relative to OFW, Momentum drops `hotels.c` and adds these 14 parser sources:

- `charliecard.c`
- `csc.c`
- `emv.c`
- `kazan.c`
- `metromoney.c`
- `saflok.c`
- `sevppk_tk.c`
- `sk_tk.c`
- `smartrider.c`
- `sonicare.c`
- `szppk_so.c`
- `ventra.c`
- `zolotaya_korona.c`
- `zolotaya_korona_online.c`

Parser presence means the firmware contains card-specific interpretation logic; it does not guarantee all cards, revisions, keys, balances, or transaction histories can be read.

### User workflow

Momentum adds a generic write scene that delegates lifecycle events to protocol-specific support (`MNTM: applications/main/nfc/scenes/nfc_scene_write.c:1-13`). The common state machine handles searching, writing, success, failure, wrong-card, retry, and poller cleanup (`MNTM: applications/main/nfc/helpers/protocol_support/nfc_protocol_support.c:897-1062`). This is a restructuring of write behavior, not a claim that OFW cannot write NFC data: OFW contains protocol-specific write workflows.

Momentum also adds a MIFARE Classic key report showing per-sector access bits and A/B keys (`MNTM: applications/main/nfc/scenes/nfc_scene_mf_classic_show_keys.c:15-72`) and an EMV transaction view (`MNTM: applications/main/nfc/scenes/nfc_scene_emv_transactions.c:6-18`).

## LF RFID and iButton

### LF RFID

OFW's LF RFID enum contains 24 entries (`OFW: lib/lfrfid/protocols/lfrfid_protocols.h:11-38`). Momentum inserts `Indala224` and `InstaFob`, producing 26 entries, and wires both into the protocol table (`MNTM: lib/lfrfid/protocols/lfrfid_protocols.h:11-40`, `MNTM: lib/lfrfid/protocols/lfrfid_protocols.c:27-54`).

OFW's extra actions offer ASK, PSK, and debug-gated RAW reading (`OFW: applications/main/lfrfid/scenes/lfrfid_scene_extra_actions.c:16-70`). Momentum adds Clear T5577 Password and debug-gated RAW emulation (`MNTM: applications/main/lfrfid/scenes/lfrfid_scene_extra_actions.c:18-100`) and adds saved-key “Write and set password” plus dedicated enter/clear/write scenes. These are powerful credential-lab tools; they also increase the number of ways a user can alter a rewritable tag.

### iButton

Momentum adds DS1420 family-code `0x81` support with read, writable-ID, emulation, save/load, validation, and edit hooks (`MNTM: lib/ibutton/protocols/dallas/protocol_ds1420.c:11-57`). It is included in the Dallas protocol group (`MNTM: lib/ibutton/protocols/dallas/protocol_group_dallas_defs.c:7-15`).

## Infrared and GPIO

### Infrared universal remotes

OFW offers four universal groups: TVs, audio players, projectors, and air conditioners (`OFW: applications/main/infrared/scenes/infrared_scene_universal.c:3-42`). Momentum offers ten entry points:

- TVs
- Projectors
- Audio
- ACs
- LEDs
- Fans
- Blu-ray/DVDs
- Monitors
- Digital Signs
- Load from Library File

The menu and routing are implemented at `MNTM: applications/main/infrared/scenes/infrared_scene_universal.c:3-141`. Momentum bundles matching databases under `applications/main/infrared/resources/infrared/assets`, including `leds.ir`, `fans.ir`, `bluray_dvd.ir`, `monitor.ir`, `digital_sign.ir`, `projectors.ir`, and the inherited television/audio/AC resources.

### GPIO core tools

OFW's core GPIO screen has USB-UART Bridge, GPIO Manual Control, and 5 V output (`OFW: applications/main/gpio/scenes/gpio_scene_start.c:45-74`). Momentum adds I2C Scanner and I2C SFP entries and routes them to dedicated scenes (`MNTM: applications/main/gpio/scenes/gpio_scene_start.c:9-15`, `MNTM: applications/main/gpio/scenes/gpio_scene_start.c:54-118`). The scanner probes all non-reserved external-bus addresses and records devices that acknowledge (`MNTM: applications/main/gpio/gpio_i2c_scanner_control.c:4-23`).

This is only the core app difference. Momentum's external pack adds dozens of GPIO tools for ESP, nRF24, GPS, VGM, MALVEKE, MAYHEM, FlipBoard, sensors, debuggers, CAN, UART, SPI, radio modules, and network co-processors; the complete categorized inventory appears later.

## Bad USB, HID remotes, and device identity

### Bad USB becomes Bad KB

The same source directory is productized differently. OFW declares `appid="bad_usb"`, name `Bad USB`, category USB (`OFW: applications/main/bad_usb/application.fam:1-13`). Momentum declares `appid="bad_kb"`, name `Bad KB`, category Tools, while noting that the code directory retained the Bad USB name for update merging (`MNTM: applications/main/bad_usb/application.fam:1-15`). The main-app metapackage requires `bad_kb` rather than `bad_usb` (`MNTM: applications/main/application.fam:5-17`).

OFW's configuration screen has global keyboard layout and BLE unpairing (`OFW: applications/main/bad_usb/scenes/bad_usb_scene_config.c:3-58`). Momentum adds a USB/BLE connection switch and, depending on transport:

- BLE pairing persistence and pairing mode;
- BLE display name and MAC address editing/randomization/reset/unpair;
- USB manufacturer, product, VID, and PID editing/randomization/reset.

The menu is defined at `MNTM: applications/main/bad_usb/scenes/bad_usb_scene_config.c:3-24` and `MNTM: applications/main/bad_usb/scenes/bad_usb_scene_config.c:72-126`; the handlers that apply and persist values begin at line 142. This means Momentum can present different USB/BLE identities to a host. Such identity customization is useful for testing but should only be used on systems and workflows the owner is authorized to test.

### HID remote expansion

OFW's HID menu includes Keynote (two orientations), keyboard, media, mouse, BLE TikTok, clicker, jiggler, and BLE unpair (`OFW: applications/system/hid_app/scenes/hid_scene_start.c:4-65`). Momentum adds Numpad, Apple Music macOS, Movie, stealth jiggler, PushToTalk, and BLE remote rename, while retaining the existing modes (`MNTM: applications/system/hid_app/scenes/hid_scene_start.c:4-97`). It also gives the two FAPs distinct visible names—USB Remote and Bluetooth Remote—where OFW names both simply Remote (`OFW: applications/system/hid_app/application.fam:1-33`, `MNTM: applications/system/hid_app/application.fam:1-33`).

## Bluetooth extra beacon and FindMy Flipper

Momentum initializes an additional advertising beacon in the BT HAL and exports get/set/start/stop/state functions (`MNTM: targets/f7/furi_hal/furi_hal_bt.c:129-132`, `MNTM: targets/f7/furi_hal/furi_hal_bt.c:416-441`). FindMy Flipper is both an external Bluetooth FAP and a startup hook described by its manifest as a “BLE FindMy Location Beacon” (`MNTM: applications/system/findmy/application.fam:1-24`).

At boot or SD-card mount, the startup hook waits for BT initialization, loads saved state, and reapplies an active beacon (`MNTM: applications/system/findmy/findmy_startup.c:9-50`). State includes active flag, advertising interval, transmit power, tag type, visible-MAC choice, MAC, and advertising data (`MNTM: applications/system/findmy/findmy_state.c:9-91`). Applying state stops an existing extra beacon, configures it, updates the battery byte where applicable, and restarts it (`MNTM: applications/system/findmy/findmy_state.c:94-143`). Payload sizes are defined for Apple, Samsung, and Tile types (`MNTM: applications/system/findmy/findmy_state.c:182-191`).

This code implements BLE advertisements compatible with those selected payload forms. It does not itself contain a global location network, cloud account, or map service; any downstream location behavior depends on external ecosystems and correctly provisioned payload data.

## JavaScript runtime and developer-facing modules

OFW treats the JavaScript runner as a system app with a startup provider (`OFW: applications/system/js_app/application.fam:1-28`). In Momentum it is an external `JS Runner` FAP, with an additional CLI plugin (`MNTM: applications/system/js_app/application.fam:1-41`). Momentum retains the event loop, GUI views, notifications, BadUSB, serial, GPIO, math, and storage modules and adds these manifest plugins:

- VGM
- Sub-GHz
- Infrared
- BLE beacon
- USB disk
- I2C
- SPI

They are wired at `MNTM: applications/system/js_app/application.fam:240-294`, and the in-tree TypeScript SDK adds corresponding definitions and examples.

These are not placeholder names. Examples of implemented capability include:

- Sub-GHz radio setup, RX/idle state, RSSI, frequency/preset, and internal/external radio selection (`MNTM: applications/system/js_app/modules/js_subghz/js_subghz.c:12-170` and subsequent methods).
- BLE extra-beacon configuration, data, start/stop, and state restoration (`MNTM: applications/system/js_app/modules/js_blebeacon.c:123-220`).
- Creation and formatting of a disk image and exposure of that file as USB mass storage (`MNTM: applications/system/js_app/modules/js_usbdisk/js_usbdisk.c:75-143`, `MNTM: applications/system/js_app/modules/js_usbdisk/js_usbdisk.c:145-207`).
- Pitch, roll, yaw, and delta-yaw access for the Video Game Module IMU (`MNTM: applications/system/js_app/modules/js_vgm/js_vgm.c:13-49`, `MNTM: applications/system/js_app/modules/js_vgm/js_vgm.c:79-133`).

The result is a materially more hardware-capable scripting environment, but scripts can also control radios, USB presentation, and buses. Treat downloaded scripts as executable device code, not passive data.

## Momentum external-app distribution

Momentum adds `applications/external` as the `Next-Flip/Momentum-Apps` submodule (`MNTM: .gitmodules:1-3`), pinned in this checkout at `55a446b1b01bf2a2f98161d704e62cc47075ad30`. Its initialized contents declare **244 active `EXTERNAL` FAPs**. The other Momentum external declarations live in the root firmware tree, yielding 269 overall. The submodule contains a commented-out `subghz_remote_ofw` block; this report does not count disabled manifest text as an app.

The following is a comprehensive inventory of the external submodule's FAP `appid` values, grouped by manifest category. It describes what is bundled for building/distribution; it is not an endorsement, security audit, or claim that every app is flashed internally.

### Games — 42

`4inrow`, `air_arkanoid`, `air_labyrinth`, `arkanoid`, `asteroids`, `blackjack`, `bomberduck`, `checkers`, `chess`, `color_guess`, `doom`, `flappy_bird`, `flipper_pong`, `game15`, `game_2048`, `gameoflife`, `geometry_dash`, `heap_defence`, `jetpack_joyride`, `laser_tag`, `minesweeper_redux`, `multi_dice`, `pinball0`, `quadrastic`, `reversi`, `roots_of_life`, `rubiks_cube_scrambler`, `scorched_tanks`, `simon_says`, `slotmachine`, `snake20`, `solitaire`, `t_rex_runner`, `tama_p1`, `tanks`, `tetris`, `tictactoe`, `umpire_indicator`, `videopoker`, `yapinvaders`, `yatzee`, `zombiez`.

### Tools — 31

`.cli_gui`, `barcode_app`, `caesar_cipher`, `calculator`, `can_tools`, `cntdown_tim`, `combo_cracker`, `counter`, `dtmf_dolphin`, `flip_crypt`, `flipbip`, `flipp_pomodoro`, `flipper_wedge`, `hex_editor`, `hex_viewer`, `iconedit`, `key_copier`, `multi_converter`, `nfc_rfid_detector`, `nightstand`, `passgen`, `programmercalc`, `qrcode`, `quac`, `resistors`, `tasks`, `text_viewer`, `tone_gen`, `totp`, `upython`, `voltcalc_app`.

### GPIO — 23

`air_mouse`, `blackhat`, `can_commander`, `coleco`, `flashlight`, `flipper_spi_terminal`, `flipperscope`, `fm_radio`, `fmtx_app`, `gpio_badge`, `gpio_controller`, `gpio_explorer_app`, `gpio_reader_a`, `gpio_reader_b`, `gpio_sentry_safe`, `longwave_clock`, `magspoof`, `pokemon`, `signal_generator`, `timelapse`, `uart_terminal`, `wii_ec_anal`, `wire_tester`.

### NFC — 22

`ami_tool`, `cyborg_detector`, `iso15693_nfc_writer`, `metroflip`, `mfc_editor`, `mfkey`, `mifare_fuzzer`, `nfc_apdu_runner`, `nfc_eink`, `nfc_login`, `nfc_magic`, `nfc_maker`, `nfc_playlist`, `passy`, `picopass`, `saflip`, `seader`, `seos`, `ulc_brute`, `ulc_relay`, `ulcfkey`, `weebo`.

### Sub-GHz — 20

`chief_cooker`, `esubghz_chat`, `flipper_share`, `fmf_to_sub`, `hc11_modem`, `meal_pager`, `pocsag_pager`, `proto_pirate`, `protoview`, `radio_scanner`, `rolling_flaws`, `spectrum_analyzer`, `sub_analyzer`, `subghz_bruteforcer`, `subghz_playlist`, `subghz_playlist_creator`, `subghz_remote`, `subghz_scheduler`, `tpms`, `weather_station`.

### Media — 15

`bpm_tapper`, `etch`, `flizzer_tracker`, `fmatrix`, `fzspground`, `image_viewer`, `metronome`, `morse_code`, `music_player`, `paint`, `text2sam`, `tuning_fork`, `usb_midi`, `video_player`, `wav_player`.

### GPIO/FlipperHTTP — 11

`flip_downloader`, `flip_library`, `flip_map`, `flip_social`, `flip_telegram`, `flip_trader`, `flip_weather`, `flip_wifi`, `flip_world`, `free_roam`, `web_crawler`.

### Infrared — 11

`flame_rng`, `flipper_xremote`, `hitachi_ac_remote`, `ir_intervalometer`, `ir_remote`, `ir_scope`, `lidar_emulator`, `midea_ac_remote`, `mitsubishi_ac_remote`, `xbox_controller`, `xremote`.

### GPIO/ESP — 10

`camera_suite`, `esp32_wifi_marauder`, `esp8266_deauther`, `esp8266_ifttt_virtual_button`, `esp8266_wifi_deauther_v2`, `esp_flasher`, `evil_portal`, `ghost_esp`, `wardriver`, `wifi_scanner`.

### GPIO/Debug — 8

`avr_isp`, `dap_link`, `eth_troubleshooter`, `flip_tdi`, `i2ctools`, `ina_meter`, `spi_mem_manager`, `swd_probe`.

### GPIO/Sensors — 8

`co2_logger`, `flipper_atomicdiceroller`, `flipper_geiger`, `hc_sr04`, `lightmeter`, `radar_scanner`, `unitemp`, `uv_meter_as7331`.

### GPIO/MALVEKE — 7

`malveke_gb_cartridge`, `malveke_gb_emulator`, `malveke_gb_link_camera`, `malveke_gb_live_camera`, `malveke_gb_photo`, `malveke_gba_cartridge`, `malveke_pin_test`.

### GPIO/MAYHEM — 6

`mayhem_camera`, `mayhem_marauder`, `mayhem_morseflash`, `mayhem_motion`, `mayhem_nannycam`, `mayhem_qrcode`.

### RFID — 6

`em4100_generator`, `fuzzer_rfid`, `simultaneous_rfid_reader`, `t5577_multiwriter`, `t5577_writer`, `uhf_rfid`.

### USB — 6

`.f0_mtp`, `ldtoypad`, `mass_storage`, `portal_of_flipper`, `usb_ccb`, `xinput_controller`.

### GPIO/NRF24 — 5

`nrf24batch`, `nrf24channelscanner`, `nrf24mousejacker`, `nrf24scan`, `nrf24sniff`.

### GPIO/FlipBoard — 4

`flipboard_blinky`, `flipboard_keyboard`, `flipboard_signal`, `flipboard_simon`.

### Bluetooth — 3

`ble_spam`, `bt_trigger`, `pc_monitor`.

### GPIO/GPS — 2

`gps_nmea`, `nearby_files`.

### GPIO/VGM — 2

`vgm_air_mouse`, `video_game_module_tool`.

### iButton — 2

`fuzzer_ibtn`, `ibutton_converter`.

This distribution model gives Momentum much greater out-of-box breadth, but it also means source provenance, permissions, storage usage, and compatibility vary by app. Many GPIO apps require separate boards or modules; an app appearing in the bundle does not imply stock Flipper hardware can perform its advertised workflow alone.

## Build system, submodules, SDK, and release engineering

### Default build posture

OFW defaults to `FIRMWARE_ORIGIN="Official"`, `COMPACT=0`, `DEBUG=1`, and `DIST_SUFFIX="local"` (`OFW: fbt_options.py:6-19`). Momentum defaults to `FIRMWARE_ORIGIN="Momentum"`, `COMPACT=1`, and `DEBUG=0`; it derives release or `mntm-branch-commit` suffixes from Git (`MNTM: fbt_options.py:7-43`). In practical terms, a plain local OFW build is debug-oriented while a plain Momentum build is size-oriented.

Momentum also sets `SKIP_EXTERNAL=False` and supports an `EXTRA_EXT_APPS` list (`MNTM: fbt_options.py:45-49`). Its firmware environment excludes the external-app submodule from core lint and forces external apps off for unit-test appsets (`MNTM: firmware.scons:30-36`, `MNTM: firmware.scons:80-104`). Distribution resources explicitly include external Dolphin and asset-pack output (`MNTM: firmware.scons:153-170`).

The default debugger configuration differs: OFW selects ST-Link with `hla_swd` (`OFW: fbt_options.py:44-53`), while Momentum selects CMSIS-DAP with SWD (`MNTM: fbt_options.py:74-83`). Both can be overridden, but a developer's unqualified `./fbt flash` environment is not identical.

### Submodules

Momentum makes three dependency-level changes visible in `.gitmodules`:

1. Adds `applications/external` from `Next-Flip/Momentum-Apps` (`MNTM: .gitmodules:1-3`).
2. Uses `Next-Flip/flipperzero-protobuf` rather than OFW's `flipperdevices/flipperzero-protobuf` (`MNTM: .gitmodules:4-7`; `OFW: .gitmodules:7-10`).
3. Adds `lib/uzlib` (`MNTM: .gitmodules:41-43`).

The inspected Momentum checkout pins the app pack at `55a446b…`, protobuf at `ea4f185…`, and uzlib at `6d60d65…`. OFW's inspected protobuf pin is `1c84fa…`. The different protobuf fork means RPC/schema compatibility should be checked at the actual firmware/API version rather than assumed solely from shared source ancestry.

### Libraries and FAP API

Momentum adds `uzlib` and `momentum` to the core library build and adds a second icon include directory (`MNTM: lib/SConscript:4-50`). Its F7 API symbol CSV has 4,677 lines versus OFW's 4,169—a net 508-line increase at these snapshots. Line count alone is not an ABI guarantee, but it demonstrates that Momentum exposes more symbols to FAPs.

Momentum CI explicitly compares its API version against OFW's release-channel API before building distribution artifacts (`MNTM: .github/workflows/build.yml:65-72`). That is evidence of intentional compatibility management, not proof that every OFW-catalog FAP or every older Momentum FAP will load forever. FAP compatibility still depends on the API version, required symbols, app manifest, and any fork-specific libraries.

### CI emphasis

The inspected OFW tree has dedicated workflows for normal build, compact build, documentation, lint/submodule integrity, PVS-Studio static analysis, physical-device unit tests, and physical-device updater tests. In particular, the unit workflow flashes a test firmware to a bench device and runs `scripts/testops.py run_units` (`OFW: .github/workflows/unit_tests.yml:21-60`), while the updater workflow flashes and validates packages on a device (`OFW: .github/workflows/updater_test.yml:12-65`).

Momentum's inspected workflow set is Build, Lint, PR Cleanup, Release, and Webhook. Its Build workflow performs firmware/updater/FAP distribution and API comparison (`MNTM: .github/workflows/build.yml:65-72`, `MNTM: .github/workflows/build.yml:108-144`); Lint invokes `./fbt lint_all` (`MNTM: .github/workflows/lint.yml:16-26`). No separate checked-in Momentum workflow in this snapshot corresponds to OFW's hardware-bench unit or updater jobs. That is a statement about committed CI configuration, not a claim that Momentum developers never test on hardware.

## Test-tree comparison

Under `applications/debug/unit_tests`, OFW has 170 files and Momentum 179. They share 165 paths; eight common files differ. OFW has five Sub-GHz vectors absent from Momentum (`cenmax_raw.sub`, `elplast.sub`, `elplast_raw.sub`, `kia_seed_raw.sub`, `scher_khan_magic_code.sub`). Momentum has fourteen absent from OFW, covering Acurite 592TXR, Bresser 3ch variants, Hormann BiSecur, Legrand, Solight TE44, and Vauno EN8822C.

Changed common unit-test files include the test runner, main unit-test registration, argument tests, LF RFID protocol tests, RPC tests, Sub-GHz tests, the resource manifest, and one basic JavaScript test. These differences track the forked implementations, but file counts do not prove greater coverage or reliability.

No firmware build, unit suite, updater test, or physical-radio test was run for this report because the requested artifact is a static comparison document and the relevant upstream E2E workflows require Flipper hardware. The source inventories and cited paths were validated locally; runtime claims are deliberately limited to what the code implements.

## Storage, updates, and switching considerations

Much of the standard file-format and storage machinery is inherited, so saved `.nfc`, `.sub`, `.rfid`, `.ir`, `.ibtn`, `.fap`, and `.js` workflows remain recognizable. Momentum nevertheless adds fork-specific state and resources, including:

- `/.momentum_settings.txt` in internal storage;
- `/.rgb_backlight.settings` for the optional RGB driver;
- `/ext/subghz/assets/extend_range.txt` for extended/radio-bypass flags;
- FindMy state and payload data;
- `/ext/asset_packs/<pack>/Anims`, `Icons`, and `Fonts`;
- a much larger `/ext/apps` distribution and extra recognized file types.

Practical switching implications are:

- Back up the SD card and any irreplaceable credentials before changing firmware.
- Do not assume fork-specific settings will be consumed by OFW; most will simply be unused files, but format/migration behavior can change.
- Reinstall the resource/app bundle matching the flashed firmware. A firmware image and an SD-card app/resource set from different builds can have API or asset mismatches.
- Recheck Sub-GHz settings after switching. OFW does not implement Momentum's extended-range/bypass file path, while Momentum loads it at boot or card mount.
- Recheck PIN/wipe and locked-RPC options after installing Momentum; their defaults and consequences differ from OFW.
- Rebuild third-party FAPs against the target firmware SDK when API versions or required fork libraries differ.

## Risk and maintenance interpretation

The following are reasoned consequences of the observed code, not measured defect rates:

| Dimension | OFW tendency | Momentum tendency |
|---|---|---|
| Support path | Official docs, issue tracker, updater, and vendor release channel | Community project, Momentum release/update infrastructure, app submodule maintainers |
| Change surface | Smaller and more conservative | 805 modified common files plus 1,319 root paths unique to Momentum |
| App provenance | Core tree plus separate official catalog | Large pinned multi-author app bundle included in the source checkout |
| RF policy | Region enforcement in the normal HAL path | Region enforcement by default, but user-accessible bypass and wider hardware-valid ranges |
| Customization failure modes | Fewer global theme/policy hooks | Asset/font replacement, cross-service settings, identity spoofing, and hardware-mod paths add integration points |
| Lock security | USB CLI disabled for PIN lock | USB and BLE RPC rejected by default while locked, with explicit opt-outs |
| Destructive controls | Standard storage controls | Optional format-after-ten-failures and a full-device wipe workflow |
| Developer compatibility | Canonical SDK/API | Larger fork API with an OFW-version compatibility check, plus fork-specific libraries/modules |
| CI visible in repository | Dedicated build, lint, docs, static analysis, bench-unit, and updater workflows | Build, lint, release, cleanup, webhook; no separate bench-test workflow in this snapshot |

A larger code and app surface is not automatically less reliable, and a smaller one is not automatically secure. It does mean Momentum has more components, maintainers, hardware combinations, and policy interactions that must be integrated and reviewed. Conversely, Momentum's explicit locked-RPC checks demonstrate that a fork can tighten a specific security behavior even while broadening the overall feature surface.

## Which should be used?

Choose **OFW** when the primary goal is:

- the vendor-supported reference behavior;
- the canonical SDK, docs, updater, and issue-reporting baseline;
- conservative Sub-GHz transmit policy without a user-facing region bypass;
- a smaller built-in and checked-out feature surface;
- reproducing a bug against upstream before reporting it.

Choose **Momentum** when the primary goal is:

- deep on-device customization of menus, assets, status bar, lock screen, Archive, shortcuts, identity, and Dolphin state;
- a ready-to-build, categorized collection of hundreds of community FAPs;
- added NFC/LF RFID/iButton/infrared/GPIO/Sub-GHz workflows;
- richer USB/BLE HID identity and remote controls;
- hardware experimentation with external CC1101, nRF24, GPS/NMEA, ESP, VGM, RGB backlight, and other modules;
- a broader JavaScript and FAP API surface.

For development or security research, keeping both is useful: reproduce on OFW to determine whether behavior is upstream, then reproduce on Momentum to isolate fork-specific code. For day-to-day ownership, the decisive question is not “which has more features?”—Momentum clearly does—but whether those additional global controls, radio options, apps, and maintenance dependencies are worth the added operational responsibility.

## Appendix: literal directory and file differences

This appendix is the literal root-repository tree comparison that the quantitative and behavioral sections summarize. It was generated from the two recorded commits with `git ls-tree -r HEAD`, sorted by complete path, and reconciled against the counts above. It compares Git-tracked entries, so it is deterministic and does not mix build products, ignored files, or untracked local state into the result.

Git does not track empty directories. The directory inventory below is therefore the set of parent directories implied by tracked paths; a submodule gitlink is also treated as a directory. Files inside initialized submodules are not flattened into the parent repository's tracked-file lists: each submodule is represented by its gitlink path and pinned commit in the dedicated table. The Momentum external-app submodule is described by app ID earlier in this report.

### Exact directory-structure delta

| Directory measure | OFW | MNTM |
|---|---:|---:|
| Logical directories implied by root tracked entries | 540 | 638 |
| Logical directory paths common to both trees | 528 | 528 |
| Logical directory paths only in that tree | 12 | 110 |

<details>
<summary>OFW-only directories — all 12 paths</summary>

```text
.github/actions
.github/actions/submit_sdk
applications/system/snake_game
assets/dolphin/internal/L1_Tv_128x47
assets/icons/Animations/Levelup1_128x64
assets/icons/Animations/Levelup2_128x64
assets/icons/MainMenu/Debug_14
assets/icons/MainMenu/FileManager_14
assets/icons/SubGhz/SubGhz_External_ant
assets/icons/SubGhz/SubGhz_Internal_ant
documentation/testing
documentation/testing/images
```

</details>

<details>
<summary>MNTM-only directories — all 110 paths</summary>

```text
.cursor
.cursor/rules
.github/workflow_data
applications/debug/ccid_test
applications/debug/ccid_test/client
applications/debug/ccid_test/iso7816
applications/external
applications/main/bad_usb/resources/badusb/Demos
applications/main/infrared/resources/infrared/remote
applications/main/momentum_app
applications/main/momentum_app/mock_imports
applications/main/momentum_app/scenes
applications/main/nfc/cli/commands/dump/protocols/ntag4xx
applications/main/nfc/cli/commands/dump/protocols/type_4_tag
applications/main/nfc/helpers/protocol_support/emv
applications/main/nfc/helpers/protocol_support/ntag4xx
applications/main/nfc/helpers/protocol_support/type_4_tag
applications/main/subghz/resources/subghz/Tesla
applications/main/subghz/resources/subghz/playlist
applications/main/subghz/resources/subghz/remote
applications/settings/bt_settings_app/mock_imports
applications/settings/desktop_settings/mock_imports
applications/settings/dolphin_passport/mock_imports
applications/settings/expansion_settings_app/mock_imports
applications/settings/input_settings_app
applications/settings/input_settings_app/mock_imports
applications/settings/notification_settings/mock_imports
applications/settings/power_settings_app/mock_imports
applications/system/findmy
applications/system/findmy/helpers
applications/system/findmy/icons
applications/system/findmy/scenes
applications/system/findmy/views
applications/system/hid_app/helpers
applications/system/js_app/examples/apps/Scripts/Examples
applications/system/js_app/modules/js_infrared
applications/system/js_app/modules/js_subghz
applications/system/js_app/modules/js_usbdisk
applications/system/js_app/modules/js_vgm
applications/system/js_app/modules/js_vgm/ICM42688P
applications/system/js_app/packages/fz-sdk/blebeacon
applications/system/js_app/packages/fz-sdk/i2c
applications/system/js_app/packages/fz-sdk/infrared
applications/system/js_app/packages/fz-sdk/spi
applications/system/js_app/packages/fz-sdk/subghz
applications/system/js_app/packages/fz-sdk/usbdisk
applications/system/js_app/packages/fz-sdk/vgm
assets/dolphin/external/L1_3d_printing_128x64
assets/dolphin/external/L1_Tv_128x47
assets/dolphin/external/L1_Wardriving_128x64
assets/dolphin/internal/L1_AnimationError_128x64
assets/icons/Animations/Levelup_128x64
assets/icons/ControlCenter
assets/icons/MainMenu/Momentum_14
assets/packs
assets/packs/Momentum
assets/packs/Momentum/Anims
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64
assets/packs/Momentum/Icons
assets/packs/Momentum/Icons/Animations
assets/packs/Momentum/Icons/Animations/Levelup_128x64
assets/packs/Momentum/Icons/BLE
assets/packs/Momentum/Icons/Dolphin
assets/packs/Momentum/Icons/Infrared
assets/packs/Momentum/Icons/Interface
assets/packs/Momentum/Icons/NFC
assets/packs/Momentum/Icons/Passport
assets/packs/Momentum/Icons/RFID
assets/packs/Momentum/Icons/Settings
assets/packs/Momentum/Icons/SubGhz
assets/packs/Momentum/Icons/U2F
assets/packs/Momentum/Icons/iButton
assets/packs/WatchDogs
assets/packs/WatchDogs/Anims
assets/packs/WatchDogs/Anims/BOTTY_CALL
assets/packs/WatchDogs/Anims/DEDSEC_AD
assets/packs/WatchDogs/Anims/DEDSEC_ANIM
assets/packs/WatchDogs/Anims/DEDSEC_ASCII
assets/packs/WatchDogs/Anims/DEDSEC_LOGO
assets/packs/WatchDogs/Anims/DEDSEC_OLD
assets/packs/WatchDogs/Anims/DEDSEC_TALK
assets/packs/WatchDogs/Anims/DEDSEC_WAVE
assets/packs/WatchDogs/Anims/FINGER
assets/packs/WatchDogs/Anims/GUNS_CAR
assets/packs/WatchDogs/Anims/HANDS
assets/packs/WatchDogs/Anims/JOIN_US
assets/packs/WatchDogs/Anims/LOGO_WD2
assets/packs/WatchDogs/Anims/MARCUS
assets/packs/WatchDogs/Anims/MUMMY
assets/packs/WatchDogs/Anims/REAPER
assets/packs/WatchDogs/Anims/REAPER_ALT
assets/packs/WatchDogs/Anims/SKULL
assets/packs/WatchDogs/Anims/SKULL_SPIN
assets/packs/WatchDogs/Anims/SPIRAL
assets/packs/WatchDogs/Fonts
assets/packs/WatchDogs/Icons
assets/packs/WatchDogs/Icons/Dolphin
assets/packs/WatchDogs/Icons/NFC
assets/packs/WatchDogs/Icons/Passport
assets/packs/WatchDogs/Icons/RFID
assets/packs/WatchDogs/Icons/SubGhz
assets/slideshow/firstboot
lib/momentum
lib/nfc/protocols/emv
lib/nfc/protocols/ntag4xx
lib/nfc/protocols/type_4_tag
lib/uzlib
scripts/User
```

</details>

### Directory-by-directory delta rollup

Every count in these tables represents a tracked entry in one of five disjoint comparison categories, except that the two Momentum-only gitlinks are naturally included in the MNTM-only column. “Changed content” means the Git blob object differs at the same path. “Mode only” means the blob is byte-identical but the tracked mode differs. The changed-gitlink column records a shared submodule path pinned to different commits.

#### Top-level rollup

| Location | OFW-only | MNTM-only | Changed-content shared blobs | Mode-only shared blobs | Changed shared gitlinks |
|---|---:|---:|---:|---:|---:|
| `(repository root)` | 1 | 2 | 8 | 0 | 0 |
| `.cursor` | 0 | 1 | 0 | 0 | 0 |
| `.github` | 13 | 21 | 5 | 0 | 0 |
| `.vscode` | 0 | 0 | 2 | 0 | 0 |
| `applications` | 63 | 339 | 439 | 19 | 0 |
| `assets` | 116 | 780 | 53 | 113 | 1 |
| `documentation` | 15 | 13 | 17 | 0 | 0 |
| `furi` | 0 | 0 | 9 | 0 | 0 |
| `lib` | 6 | 148 | 226 | 0 | 0 |
| `scripts` | 0 | 11 | 21 | 0 | 0 |
| `site_scons` | 0 | 0 | 2 | 0 | 0 |
| `targets` | 0 | 4 | 23 | 0 | 0 |

#### Second-level rollup

Only locations containing at least one delta are shown.

| Location | OFW-only | MNTM-only | Changed-content shared blobs | Mode-only shared blobs | Changed shared gitlinks |
|---|---:|---:|---:|---:|---:|
| `(repository root)` | 1 | 2 | 8 | 0 | 0 |
| `.cursor/rules` | 0 | 1 | 0 | 0 | 0 |
| `.github` | 0 | 2 | 2 | 0 | 0 |
| `.github/ISSUE_TEMPLATE` | 0 | 2 | 2 | 0 | 0 |
| `.github/actions` | 1 | 0 | 0 | 0 | 0 |
| `.github/assets` | 4 | 9 | 0 | 0 | 0 |
| `.github/workflow_data` | 0 | 4 | 0 | 0 | 0 |
| `.github/workflows` | 8 | 4 | 1 | 0 | 0 |
| `.vscode/example` | 0 | 0 | 2 | 0 | 0 |
| `applications` | 0 | 1 | 1 | 0 | 0 |
| `applications/debug` | 5 | 27 | 15 | 0 | 0 |
| `applications/drivers` | 0 | 0 | 3 | 0 | 0 |
| `applications/examples` | 0 | 0 | 1 | 0 | 0 |
| `applications/main` | 27 | 159 | 191 | 0 | 0 |
| `applications/services` | 2 | 14 | 143 | 0 | 0 |
| `applications/settings` | 3 | 22 | 43 | 0 | 0 |
| `applications/system` | 26 | 116 | 42 | 19 | 0 |
| `assets` | 0 | 0 | 2 | 0 | 1 |
| `assets/dolphin` | 25 | 66 | 47 | 113 | 0 |
| `assets/icons` | 91 | 115 | 3 | 0 | 0 |
| `assets/packs` | 0 | 592 | 0 | 0 | 0 |
| `assets/slideshow` | 0 | 7 | 1 | 0 | 0 |
| `documentation` | 0 | 11 | 0 | 0 | 0 |
| `documentation/devboard` | 0 | 0 | 1 | 0 | 0 |
| `documentation/doxygen` | 0 | 0 | 3 | 0 | 0 |
| `documentation/file_formats` | 0 | 1 | 2 | 0 | 0 |
| `documentation/images` | 0 | 0 | 1 | 0 | 0 |
| `documentation/js` | 0 | 1 | 10 | 0 | 0 |
| `documentation/testing` | 15 | 0 | 0 | 0 | 0 |
| `furi` | 0 | 0 | 2 | 0 | 0 |
| `furi/core` | 0 | 0 | 7 | 0 | 0 |
| `lib` | 0 | 2 | 1 | 0 | 0 |
| `lib/bit_lib` | 0 | 0 | 1 | 0 | 0 |
| `lib/ble_profile` | 0 | 0 | 1 | 0 | 0 |
| `lib/drivers` | 0 | 5 | 1 | 0 | 0 |
| `lib/fatfs` | 0 | 0 | 1 | 0 | 0 |
| `lib/flipper_application` | 0 | 0 | 2 | 0 | 0 |
| `lib/ibutton` | 0 | 2 | 2 | 0 | 0 |
| `lib/infrared` | 0 | 0 | 6 | 0 | 0 |
| `lib/lfrfid` | 0 | 4 | 14 | 0 | 0 |
| `lib/mjs` | 0 | 0 | 1 | 0 | 0 |
| `lib/momentum` | 0 | 10 | 0 | 0 | 0 |
| `lib/music_worker` | 0 | 0 | 1 | 0 | 0 |
| `lib/nfc` | 0 | 33 | 47 | 0 | 0 |
| `lib/signal_reader` | 0 | 0 | 1 | 0 | 0 |
| `lib/subghz` | 6 | 88 | 128 | 0 | 0 |
| `lib/toolbox` | 0 | 4 | 15 | 0 | 0 |
| `lib/u8g2` | 0 | 0 | 3 | 0 | 0 |
| `lib/update_util` | 0 | 0 | 1 | 0 | 0 |
| `scripts` | 0 | 5 | 6 | 0 | 0 |
| `scripts/User` | 0 | 6 | 0 | 0 | 0 |
| `scripts/debug` | 0 | 0 | 1 | 0 | 0 |
| `scripts/fbt` | 0 | 0 | 3 | 0 | 0 |
| `scripts/fbt_tools` | 0 | 0 | 7 | 0 | 0 |
| `scripts/flipper` | 0 | 0 | 2 | 0 | 0 |
| `scripts/toolchain` | 0 | 0 | 1 | 0 | 0 |
| `scripts/ufbt` | 0 | 0 | 1 | 0 | 0 |
| `site_scons` | 0 | 0 | 2 | 0 | 0 |
| `targets/f18` | 0 | 0 | 1 | 0 | 0 |
| `targets/f7` | 0 | 3 | 18 | 0 | 0 |
| `targets/furi_hal_include` | 0 | 1 | 4 | 0 | 0 |

### Submodule and gitlink differences

The parent repositories share twelve submodule paths. Eleven pins are identical; `assets/protobuf` is pinned differently. Momentum also adds the `applications/external` and `lib/uzlib` gitlinks.

| Gitlink path | Relationship | OFW pinned commit | MNTM pinned commit |
|---|---|---|---|
| `applications/external` | MNTM only | — | `55a446b1b01bf2a2f98161d704e62cc47075ad30` |
| `assets/protobuf` | shared pin differs | `1c84fa48919cbb71d1cc65236fc0ee36740e24c6` | `ea4f185f5eaa265955c520eae2832887ee6aa5e4` |
| `documentation/doxygen/doxygen-awesome-css` | shared pin identical | `df88fe4fdd97714fadfd3ef17de0b4401f804052` | `df88fe4fdd97714fadfd3ef17de0b4401f804052` |
| `lib/FreeRTOS-Kernel` | shared pin identical | `def7d2df2b0506d3d249334974f51e427c17a41c` | `def7d2df2b0506d3d249334974f51e427c17a41c` |
| `lib/heatshrink` | shared pin identical | `7398ccc91652a33483245200cfa1a83b073bc206` | `7398ccc91652a33483245200cfa1a83b073bc206` |
| `lib/libusb_stm32` | shared pin identical | `6ca2857519f996244f7b324dd227fdf0a075fffb` | `6ca2857519f996244f7b324dd227fdf0a075fffb` |
| `lib/mbedtls` | shared pin identical | `107ea89daaefb9867ea9121002fbbdf926780e98` | `107ea89daaefb9867ea9121002fbbdf926780e98` |
| `lib/microtar` | shared pin identical | `1e921369b2c92bb219fcef84a37d4d2347794c0f` | `1e921369b2c92bb219fcef84a37d4d2347794c0f` |
| `lib/mlib` | shared pin identical | `62c8ac3e5d4a7a4f8757328e7a80286fde2686b6` | `62c8ac3e5d4a7a4f8757328e7a80286fde2686b6` |
| `lib/nanopb` | shared pin identical | `6cfe48d6f1593f8fa5c0f90437f5e6522587745e` | `6cfe48d6f1593f8fa5c0f90437f5e6522587745e` |
| `lib/stm32wb_cmsis` | shared pin identical | `d1b860584dfe24d40d455ae624ed14600dfa93c9` | `d1b860584dfe24d40d455ae624ed14600dfa93c9` |
| `lib/stm32wb_copro` | shared pin identical | `133182d5583e998bb263cd947105be4df9c29cb3` | `133182d5583e998bb263cd947105be4df9c29cb3` |
| `lib/stm32wb_hal` | shared pin identical | `cfd0dd258cb031c95b2b2d6d04c19f9f625fe3e8` | `cfd0dd258cb031c95b2b2d6d04c19f9f625fe3e8` |
| `lib/uzlib` | MNTM only | — | `6d60d651a4499a64f2e5b21b4cc08d98cb84b5c1` |

### Paths tracked only by OFW — 214

These are all root-repository entries present at the OFW snapshot and absent at the MNTM snapshot. All 214 are blobs; none is a submodule gitlink.

<details>
<summary>Show all 214 OFW-only tracked paths</summary>

```text
.github/actions/submit_sdk/action.yml
.github/assets/Born2bSportyV2.ttf
.github/assets/dark_theme_banner.png
.github/assets/latest-firmware-template.png
.github/assets/light_theme_banner.png
.github/workflows/build_compact.yml
.github/workflows/docs.yml
.github/workflows/lint_and_submodule_check.yml
.github/workflows/merge_report.yml
.github/workflows/pvs_studio.yml
.github/workflows/reindex.yml
.github/workflows/unit_tests.yml
.github/workflows/updater_test.yml
CODE_OF_CONDUCT.md
applications/debug/unit_tests/resources/unit_tests/subghz/cenmax_raw.sub
applications/debug/unit_tests/resources/unit_tests/subghz/elplast.sub
applications/debug/unit_tests/resources/unit_tests/subghz/elplast_raw.sub
applications/debug/unit_tests/resources/unit_tests/subghz/kia_seed_raw.sub
applications/debug/unit_tests/resources/unit_tests/subghz/scher_khan_magic_code.sub
applications/main/bad_usb/resources/badusb/Install_qFlipper_gnome.txt
applications/main/bad_usb/resources/badusb/Install_qFlipper_macOS.txt
applications/main/bad_usb/resources/badusb/Install_qFlipper_windows.txt
applications/main/bad_usb/resources/badusb/assets/layouts/ja-JP.kl
applications/main/bad_usb/resources/badusb/demo_chromeos.txt
applications/main/bad_usb/resources/badusb/demo_gnome.txt
applications/main/bad_usb/resources/badusb/demo_macos.txt
applications/main/bad_usb/resources/badusb/demo_windows.txt
applications/main/bad_usb/resources/badusb/test_mouse.txt
applications/main/bad_usb/scenes/bad_usb_scene_unpair_done.c
applications/main/infrared/resources/infrared/assets/projector.ir
applications/main/nfc/helpers/protocol_support/iso14443_3b/iso14443_3b_i.h
applications/main/nfc/helpers/protocol_support/nfc_protocol_support_defs.c
applications/main/nfc/helpers/protocol_support/nfc_protocol_support_defs.h
applications/main/nfc/plugins/supported_cards/hotels.c
applications/main/nfc/scenes/nfc_scene_mf_classic_write_initial.c
applications/main/nfc/scenes/nfc_scene_mf_classic_write_initial_fail.c
applications/main/nfc/scenes/nfc_scene_mf_classic_write_initial_success.c
applications/main/nfc/scenes/nfc_scene_mf_classic_write_initial_wrong_card.c
applications/main/nfc/scenes/nfc_scene_mf_ultralight_write.c
applications/main/nfc/scenes/nfc_scene_mf_ultralight_write_fail.c
applications/main/nfc/scenes/nfc_scene_mf_ultralight_write_success.c
applications/main/nfc/scenes/nfc_scene_mf_ultralight_wrong_card.c
applications/main/subghz/helpers/subghz_frequency_analyzer_log_item_array.c
applications/main/subghz/helpers/subghz_frequency_analyzer_log_item_array.h
applications/main/subghz/scenes/subghz_scene_radio_setting.c
applications/main/subghz/scenes/subghz_scene_region_info.c
applications/services/desktop/scenes/desktop_scene_debug.c
applications/services/desktop/views/desktop_view_debug.c
applications/settings/desktop_settings/scenes/desktop_settings_scene_favorite.c
applications/settings/desktop_settings/scenes/desktop_settings_scene_quick_apps_direction_menu.c
applications/settings/desktop_settings/scenes/desktop_settings_scene_quick_apps_menu.c
applications/system/hid_app/assets/Dpad_49x46.png
applications/system/hid_app/assets/Left_mouse_icon_9x10.png
applications/system/hid_app/assets/Like_def_13x11.png
applications/system/hid_app/assets/Like_pressed_17x16.png
applications/system/hid_app/assets/Ok_btn_pressed_13x12.png
applications/system/hid_app/assets/Right_mouse_icon_9x10.png
applications/system/js_app/examples/apps/Scripts/array_buf_test.js
applications/system/js_app/examples/apps/Scripts/bad_uart.js
applications/system/js_app/examples/apps/Scripts/badusb_demo.js
applications/system/js_app/examples/apps/Scripts/console.js
applications/system/js_app/examples/apps/Scripts/delay.js
applications/system/js_app/examples/apps/Scripts/event_loop.js
applications/system/js_app/examples/apps/Scripts/gpio.js
applications/system/js_app/examples/apps/Scripts/gui.js
applications/system/js_app/examples/apps/Scripts/load.js
applications/system/js_app/examples/apps/Scripts/load_api.js
applications/system/js_app/examples/apps/Scripts/math.js
applications/system/js_app/examples/apps/Scripts/notify.js
applications/system/js_app/examples/apps/Scripts/path.js
applications/system/js_app/examples/apps/Scripts/storage.js
applications/system/js_app/examples/apps/Scripts/stringutils.js
applications/system/js_app/examples/apps/Scripts/uart_echo.js
applications/system/js_app/examples/apps/Scripts/uart_echo_8e1.js
applications/system/snake_game/application.fam
applications/system/snake_game/snake_10px.png
applications/system/snake_game/snake_game.c
assets/dolphin/blocking/L0_NoDb_128x51/frame_1.png
assets/dolphin/blocking/L0_NoDb_128x51/frame_2.png
assets/dolphin/blocking/L0_NoDb_128x51/frame_3.png
assets/dolphin/blocking/L0_SdBad_128x51/frame_1.png
assets/dolphin/blocking/L0_SdOk_128x51/frame_1.png
assets/dolphin/blocking/L0_SdOk_128x51/frame_2.png
assets/dolphin/blocking/L0_SdOk_128x51/frame_3.png
assets/dolphin/blocking/L0_Url_128x51/frame_1.png
assets/dolphin/blocking/L0_Url_128x51/frame_2.png
assets/dolphin/blocking/L0_Url_128x51/frame_3.png
assets/dolphin/internal/L1_BadBattery_128x47/frame_1.png
assets/dolphin/internal/L1_NoSd_128x49/frame_1.png
assets/dolphin/internal/L1_NoSd_128x49/frame_2.png
assets/dolphin/internal/L1_NoSd_128x49/frame_3.png
assets/dolphin/internal/L1_NoSd_128x49/frame_4.png
assets/dolphin/internal/L1_NoSd_128x49/frame_5.png
assets/dolphin/internal/L1_Tv_128x47/frame_0.png
assets/dolphin/internal/L1_Tv_128x47/frame_1.png
assets/dolphin/internal/L1_Tv_128x47/frame_2.png
assets/dolphin/internal/L1_Tv_128x47/frame_3.png
assets/dolphin/internal/L1_Tv_128x47/frame_4.png
assets/dolphin/internal/L1_Tv_128x47/frame_5.png
assets/dolphin/internal/L1_Tv_128x47/frame_6.png
assets/dolphin/internal/L1_Tv_128x47/frame_7.png
assets/dolphin/internal/L1_Tv_128x47/meta.txt
assets/icons/Animations/Levelup1_128x64/frame_00.png
assets/icons/Animations/Levelup1_128x64/frame_01.png
assets/icons/Animations/Levelup1_128x64/frame_02.png
assets/icons/Animations/Levelup1_128x64/frame_03.png
assets/icons/Animations/Levelup1_128x64/frame_04.png
assets/icons/Animations/Levelup1_128x64/frame_05.png
assets/icons/Animations/Levelup1_128x64/frame_06.png
assets/icons/Animations/Levelup1_128x64/frame_07.png
assets/icons/Animations/Levelup1_128x64/frame_08.png
assets/icons/Animations/Levelup1_128x64/frame_09.png
assets/icons/Animations/Levelup1_128x64/frame_10.png
assets/icons/Animations/Levelup1_128x64/frame_rate
assets/icons/Animations/Levelup2_128x64/frame_00.png
assets/icons/Animations/Levelup2_128x64/frame_01.png
assets/icons/Animations/Levelup2_128x64/frame_02.png
assets/icons/Animations/Levelup2_128x64/frame_03.png
assets/icons/Animations/Levelup2_128x64/frame_04.png
assets/icons/Animations/Levelup2_128x64/frame_05.png
assets/icons/Animations/Levelup2_128x64/frame_06.png
assets/icons/Animations/Levelup2_128x64/frame_07.png
assets/icons/Animations/Levelup2_128x64/frame_08.png
assets/icons/Animations/Levelup2_128x64/frame_09.png
assets/icons/Animations/Levelup2_128x64/frame_10.png
assets/icons/Animations/Levelup2_128x64/frame_rate
assets/icons/Infrared/back_btn_10x8.png
assets/icons/Infrared/hourglass0_24x24.png
assets/icons/Infrared/hourglass1_24x24.png
assets/icons/Infrared/hourglass2_24x24.png
assets/icons/Infrared/hourglass3_24x24.png
assets/icons/Infrared/hourglass4_24x24.png
assets/icons/Infrared/hourglass5_24x24.png
assets/icons/Infrared/hourglass6_24x24.png
assets/icons/Interface/DoorLeft_70x55.png
assets/icons/Interface/DoorRight_70x55.png
assets/icons/Interface/SmallArrowDown_4x7.png
assets/icons/Interface/SmallArrowUp_4x7.png
assets/icons/Keyboard/KeyBackspaceSelected_16x9.png
assets/icons/Keyboard/KeyBackspace_16x9.png
assets/icons/Keyboard/KeySaveBlockedSelected_24x11.png
assets/icons/Keyboard/KeySaveBlocked_24x11.png
assets/icons/Keyboard/KeySaveSelected_24x11.png
assets/icons/Keyboard/KeySave_24x11.png
assets/icons/MainMenu/Debug_14/frame_01.png
assets/icons/MainMenu/Debug_14/frame_02.png
assets/icons/MainMenu/Debug_14/frame_03.png
assets/icons/MainMenu/Debug_14/frame_04.png
assets/icons/MainMenu/Debug_14/frame_rate
assets/icons/MainMenu/FileManager_14/frame_01.png
assets/icons/MainMenu/FileManager_14/frame_02.png
assets/icons/MainMenu/FileManager_14/frame_03.png
assets/icons/MainMenu/FileManager_14/frame_04.png
assets/icons/MainMenu/FileManager_14/frame_05.png
assets/icons/MainMenu/FileManager_14/frame_06.png
assets/icons/MainMenu/FileManager_14/frame_07.png
assets/icons/MainMenu/FileManager_14/frame_08.png
assets/icons/MainMenu/FileManager_14/frame_09.png
assets/icons/MainMenu/FileManager_14/frame_10.png
assets/icons/MainMenu/FileManager_14/frame_rate
assets/icons/NFC/Detailed_chip_17x13.png
assets/icons/NFC/Medium-chip-22x21.png
assets/icons/Passport/passport_bad1_46x49.png
assets/icons/Passport/passport_bad2_46x49.png
assets/icons/Passport/passport_bad3_46x49.png
assets/icons/Passport/passport_bottom_128x18.png
assets/icons/Passport/passport_happy1_46x49.png
assets/icons/Passport/passport_happy2_46x49.png
assets/icons/Passport/passport_happy3_46x49.png
assets/icons/Passport/passport_left_6x46.png
assets/icons/Passport/passport_okay1_46x49.png
assets/icons/Passport/passport_okay2_46x49.png
assets/icons/Passport/passport_okay3_46x49.png
assets/icons/RFID/RFIDBigChip_37x36.png
assets/icons/StatusBar/Alert_9x8.png
assets/icons/StatusBar/Attention_5x8.png
assets/icons/StatusBar/Battery_26x8.png
assets/icons/StatusBar/Charging-lightning_9x10.png
assets/icons/StatusBar/Charging-lightning_mask_9x10.png
assets/icons/StatusBar/GameMode_11x8.png
assets/icons/SubGhz/External_ant_1_9x11.png
assets/icons/SubGhz/Internal_ant_1_9x11.png
assets/icons/SubGhz/Scanning_short_96x52.png
assets/icons/SubGhz/SubGhz_External_ant/frame_01.png
assets/icons/SubGhz/SubGhz_External_ant/frame_02.png
assets/icons/SubGhz/SubGhz_External_ant/frame_03.png
assets/icons/SubGhz/SubGhz_External_ant/frame_04.png
assets/icons/SubGhz/SubGhz_External_ant/frame_rate
assets/icons/SubGhz/SubGhz_Internal_ant/frame_01.png
assets/icons/SubGhz/SubGhz_Internal_ant/frame_02.png
assets/icons/SubGhz/SubGhz_Internal_ant/frame_03.png
assets/icons/SubGhz/SubGhz_Internal_ant/frame_04.png
assets/icons/SubGhz/SubGhz_Internal_ant/frame_rate
documentation/testing/badusb_test_cases.md
documentation/testing/cli_test_cases.md
documentation/testing/general_test_cases.md
documentation/testing/goodfaps_test_cases.md
documentation/testing/gpio_test_cases.md
documentation/testing/ibutton_test_cases.md
documentation/testing/images/flipperzero-clock-app.png
documentation/testing/images/flipperzero-devices-on-windows.png
documentation/testing/images/flipperzero-on-flipper-lab.png
documentation/testing/images/flipperzero-passport.png
documentation/testing/infrared_test_cases.md
documentation/testing/integration_tests.md
documentation/testing/nfc_test_cases.md
documentation/testing/rfid_test_cases.md
documentation/testing/subghz_test_cases.md
lib/subghz/protocols/kia.c
lib/subghz/protocols/kia.h
lib/subghz/protocols/scher_khan.c
lib/subghz/protocols/scher_khan.h
lib/subghz/protocols/star_line.c
lib/subghz/protocols/star_line.h
```

</details>

### Paths tracked only by Momentum — 1319

These are all root-repository entries present at the MNTM snapshot and absent at the OFW snapshot. Of the 1,319 entries, 1,317 are blobs and two—`applications/external` and `lib/uzlib`—are submodule gitlinks.

<details>
<summary>Show all 1319 MNTM-only tracked paths</summary>

```text
.cursor/rules/momentum.mdc
.github/FUNDING.yml
.github/ISSUE_TEMPLATE/02_enhancements.yml
.github/ISSUE_TEMPLATE/03_feature_request.yml
.github/assets/badkb.png
.github/assets/icon.png
.github/assets/logo_dark.png
.github/assets/logo_light.png
.github/assets/packs-done.png
.github/assets/packs-folder.png
.github/assets/packs-select.png
.github/assets/settings.png
.github/assets/social-preview.png
.github/copilot-instructions.md
.github/workflow_data/devbuild.py
.github/workflow_data/release.md
.github/workflow_data/release.py
.github/workflow_data/webhook.py
.github/workflows/lint.yml
.github/workflows/pr-cleanup.yml
.github/workflows/release.yml
.github/workflows/webhook.yml
AGENTS.md
CHANGELOG.md
applications/debug/ccid_test/application.fam
applications/debug/ccid_test/ccid_test_app.c
applications/debug/ccid_test/ccid_test_app_commands.c
applications/debug/ccid_test/ccid_test_app_commands.h
applications/debug/ccid_test/client/ccid_client.py
applications/debug/ccid_test/client/requirements.txt
applications/debug/ccid_test/iso7816/iso7816_atr.h
applications/debug/ccid_test/iso7816/iso7816_handler.c
applications/debug/ccid_test/iso7816/iso7816_handler.h
applications/debug/ccid_test/iso7816/iso7816_response.c
applications/debug/ccid_test/iso7816/iso7816_response.h
applications/debug/ccid_test/iso7816/iso7816_t0_apdu.c
applications/debug/ccid_test/iso7816/iso7816_t0_apdu.h
applications/debug/unit_tests/resources/unit_tests/subghz/acurite_592txr.sub
applications/debug/unit_tests/resources/unit_tests/subghz/bresser_3ch.sub
applications/debug/unit_tests/resources/unit_tests/subghz/bresser_3ch_raw.sub
applications/debug/unit_tests/resources/unit_tests/subghz/bresser_3ch_v0.sub
applications/debug/unit_tests/resources/unit_tests/subghz/bresser_3ch_v0_raw.sub
applications/debug/unit_tests/resources/unit_tests/subghz/hormann_bisecur_080ED969.sub
applications/debug/unit_tests/resources/unit_tests/subghz/hormann_bisecur_1C84A55B.sub
applications/debug/unit_tests/resources/unit_tests/subghz/hormann_bisecur_1C84A55B_raw.sub
applications/debug/unit_tests/resources/unit_tests/subghz/legrand_2E37F.sub
applications/debug/unit_tests/resources/unit_tests/subghz/legrand_2E37F_raw.sub
applications/debug/unit_tests/resources/unit_tests/subghz/solight_te44.sub
applications/debug/unit_tests/resources/unit_tests/subghz/solight_te44_raw.sub
applications/debug/unit_tests/resources/unit_tests/subghz/vauno_en8822c.sub
applications/debug/unit_tests/resources/unit_tests/subghz/vauno_en8822c_raw.sub
applications/external
applications/main/archive/helpers/archive_helpers_ext.h
applications/main/archive/helpers/archive_menu.h
applications/main/archive/helpers/favorite_timeout.c
applications/main/archive/scenes/archive_scene_info.c
applications/main/archive/scenes/archive_scene_new_dir.c
applications/main/archive/scenes/archive_scene_search.c
applications/main/bad_usb/helpers/ble_hid_ext_profile.c
applications/main/bad_usb/helpers/ble_hid_ext_profile.h
applications/main/bad_usb/resources/badusb/Demos/Install_qFlipper_gnome.txt
applications/main/bad_usb/resources/badusb/Demos/Install_qFlipper_macOS.txt
applications/main/bad_usb/resources/badusb/Demos/Install_qFlipper_windows.txt
applications/main/bad_usb/resources/badusb/Demos/demo_android.txt
applications/main/bad_usb/resources/badusb/Demos/demo_chromeos.txt
applications/main/bad_usb/resources/badusb/Demos/demo_gnome.txt
applications/main/bad_usb/resources/badusb/Demos/demo_ios.txt
applications/main/bad_usb/resources/badusb/Demos/demo_macos.txt
applications/main/bad_usb/resources/badusb/Demos/demo_windows.txt
applications/main/bad_usb/resources/badusb/Demos/test_mouse.txt
applications/main/bad_usb/resources/badusb/assets/layouts/colemak.kl
applications/main/bad_usb/resources/badusb/assets/layouts/de-DE-mac.kl
applications/main/bad_usb/resources/badusb/assets/layouts/fi-FI.kl
applications/main/bad_usb/scenes/bad_usb_scene_config_ble_mac.c
applications/main/bad_usb/scenes/bad_usb_scene_config_ble_name.c
applications/main/bad_usb/scenes/bad_usb_scene_config_usb_name.c
applications/main/bad_usb/scenes/bad_usb_scene_config_usb_vidpid.c
applications/main/bad_usb/scenes/bad_usb_scene_done.c
applications/main/gpio/gpio_i2c_scanner_control.c
applications/main/gpio/gpio_i2c_scanner_control.h
applications/main/gpio/gpio_i2c_sfp_control.c
applications/main/gpio/gpio_i2c_sfp_control.h
applications/main/gpio/scenes/gpio_scene_i2c_scanner.c
applications/main/gpio/scenes/gpio_scene_i2c_sfp.c
applications/main/gpio/views/gpio_i2c_scanner.c
applications/main/gpio/views/gpio_i2c_scanner.h
applications/main/gpio/views/gpio_i2c_sfp.c
applications/main/gpio/views/gpio_i2c_sfp.h
applications/main/infrared/infrared_settings.h
applications/main/infrared/resources/infrared/Samsung.ir
applications/main/infrared/resources/infrared/assets/bluray_dvd.ir
applications/main/infrared/resources/infrared/assets/digital_sign.ir
applications/main/infrared/resources/infrared/assets/fans.ir
applications/main/infrared/resources/infrared/assets/leds.ir
applications/main/infrared/resources/infrared/assets/monitor.ir
applications/main/infrared/resources/infrared/assets/projectors.ir
applications/main/infrared/resources/infrared/remote/Samsung_Remote.txt
applications/main/infrared/scenes/infrared_scene_universal_bluray.c
applications/main/infrared/scenes/infrared_scene_universal_digital_sign.c
applications/main/infrared/scenes/infrared_scene_universal_fan.c
applications/main/infrared/scenes/infrared_scene_universal_from_file.c
applications/main/infrared/scenes/infrared_scene_universal_leds.c
applications/main/infrared/scenes/infrared_scene_universal_monitor.c
applications/main/lfrfid/scenes/lfrfid_scene_clear_t5577.c
applications/main/lfrfid/scenes/lfrfid_scene_clear_t5577_confirm.c
applications/main/lfrfid/scenes/lfrfid_scene_enter_password.c
applications/main/lfrfid/scenes/lfrfid_scene_raw_emulate.c
applications/main/lfrfid/scenes/lfrfid_scene_select_raw_key.c
applications/main/lfrfid/scenes/lfrfid_scene_write_and_set_pass.c
applications/main/momentum_app/application.fam
applications/main/momentum_app/mock_imports/mock_desktop.c
applications/main/momentum_app/mock_imports/mock_dolphin_state.c
applications/main/momentum_app/momentum_app.c
applications/main/momentum_app/momentum_app.h
applications/main/momentum_app/scenes/momentum_app_scene.c
applications/main/momentum_app/scenes/momentum_app_scene.h
applications/main/momentum_app/scenes/momentum_app_scene_config.h
applications/main/momentum_app/scenes/momentum_app_scene_interface.c
applications/main/momentum_app/scenes/momentum_app_scene_interface_filebrowser.c
applications/main/momentum_app/scenes/momentum_app_scene_interface_general.c
applications/main/momentum_app/scenes/momentum_app_scene_interface_graphics.c
applications/main/momentum_app/scenes/momentum_app_scene_interface_graphics_pack.c
applications/main/momentum_app/scenes/momentum_app_scene_interface_lockscreen.c
applications/main/momentum_app/scenes/momentum_app_scene_interface_mainmenu.c
applications/main/momentum_app/scenes/momentum_app_scene_interface_mainmenu_add.c
applications/main/momentum_app/scenes/momentum_app_scene_interface_mainmenu_add_main.c
applications/main/momentum_app/scenes/momentum_app_scene_interface_mainmenu_reset.c
applications/main/momentum_app/scenes/momentum_app_scene_interface_mainmenu_style.c
applications/main/momentum_app/scenes/momentum_app_scene_interface_statusbar.c
applications/main/momentum_app/scenes/momentum_app_scene_misc.c
applications/main/momentum_app/scenes/momentum_app_scene_misc_dolphin.c
applications/main/momentum_app/scenes/momentum_app_scene_misc_dolphin_xp.c
applications/main/momentum_app/scenes/momentum_app_scene_misc_screen.c
applications/main/momentum_app/scenes/momentum_app_scene_misc_screen_color.c
applications/main/momentum_app/scenes/momentum_app_scene_misc_spoof.c
applications/main/momentum_app/scenes/momentum_app_scene_misc_spoof_name.c
applications/main/momentum_app/scenes/momentum_app_scene_misc_vgm.c
applications/main/momentum_app/scenes/momentum_app_scene_misc_vgm_color.c
applications/main/momentum_app/scenes/momentum_app_scene_protocols.c
applications/main/momentum_app/scenes/momentum_app_scene_protocols_freqs.c
applications/main/momentum_app/scenes/momentum_app_scene_protocols_freqs_add.c
applications/main/momentum_app/scenes/momentum_app_scene_protocols_freqs_hopper.c
applications/main/momentum_app/scenes/momentum_app_scene_protocols_freqs_static.c
applications/main/momentum_app/scenes/momentum_app_scene_protocols_gpio.c
applications/main/momentum_app/scenes/momentum_app_scene_start.c
applications/main/nfc/cli/commands/dump/protocols/ntag4xx/nfc_cli_dump_ntag4xx.c
applications/main/nfc/cli/commands/dump/protocols/ntag4xx/nfc_cli_dump_ntag4xx.h
applications/main/nfc/cli/commands/dump/protocols/type_4_tag/nfc_cli_dump_type_4_tag.c
applications/main/nfc/cli/commands/dump/protocols/type_4_tag/nfc_cli_dump_type_4_tag.h
applications/main/nfc/helpers/protocol_support/emv/emv.c
applications/main/nfc/helpers/protocol_support/emv/emv.h
applications/main/nfc/helpers/protocol_support/emv/emv_render.c
applications/main/nfc/helpers/protocol_support/emv/emv_render.h
applications/main/nfc/helpers/protocol_support/ntag4xx/ntag4xx.c
applications/main/nfc/helpers/protocol_support/ntag4xx/ntag4xx.h
applications/main/nfc/helpers/protocol_support/ntag4xx/ntag4xx_render.c
applications/main/nfc/helpers/protocol_support/ntag4xx/ntag4xx_render.h
applications/main/nfc/helpers/protocol_support/type_4_tag/type_4_tag.c
applications/main/nfc/helpers/protocol_support/type_4_tag/type_4_tag.h
applications/main/nfc/helpers/protocol_support/type_4_tag/type_4_tag_render.c
applications/main/nfc/helpers/protocol_support/type_4_tag/type_4_tag_render.h
applications/main/nfc/plugins/supported_cards/charliecard.c
applications/main/nfc/plugins/supported_cards/csc.c
applications/main/nfc/plugins/supported_cards/emv.c
applications/main/nfc/plugins/supported_cards/kazan.c
applications/main/nfc/plugins/supported_cards/metromoney.c
applications/main/nfc/plugins/supported_cards/saflok.c
applications/main/nfc/plugins/supported_cards/sevppk_tk.c
applications/main/nfc/plugins/supported_cards/sk_tk.c
applications/main/nfc/plugins/supported_cards/smartrider.c
applications/main/nfc/plugins/supported_cards/sonicare.c
applications/main/nfc/plugins/supported_cards/szppk_so.c
applications/main/nfc/plugins/supported_cards/ventra.c
applications/main/nfc/plugins/supported_cards/zolotaya_korona.c
applications/main/nfc/plugins/supported_cards/zolotaya_korona_online.c
applications/main/nfc/resources/nfc/RickRoll.nfc
applications/main/nfc/resources/nfc/assets/sev_id.nfc
applications/main/nfc/resources/nfc/assets/sk_id.nfc
applications/main/nfc/resources/nfc/assets/sz_id.nfc
applications/main/nfc/scenes/nfc_scene_emv_transactions.c
applications/main/nfc/scenes/nfc_scene_mf_classic_show_keys.c
applications/main/nfc/scenes/nfc_scene_write.c
applications/main/subghz/helpers/minmea.c
applications/main/subghz/helpers/minmea.h
applications/main/subghz/helpers/subghz_gen_info.c
applications/main/subghz/helpers/subghz_gen_info.h
applications/main/subghz/helpers/subghz_gps.c
applications/main/subghz/helpers/subghz_gps.h
applications/main/subghz/helpers/subghz_gps_plugin.c
applications/main/subghz/resources/subghz/Tesla/Tesla_EU_AM270.sub
applications/main/subghz/resources/subghz/Tesla/Tesla_EU_AM650.sub
applications/main/subghz/resources/subghz/Tesla/Tesla_US_AM270.sub
applications/main/subghz/resources/subghz/Tesla/Tesla_US_AM650.sub
applications/main/subghz/resources/subghz/assets/README.md
applications/main/subghz/resources/subghz/playlist/Tesla_Playlist.txt
applications/main/subghz/resources/subghz/remote/Tesla_Remote.txt
applications/main/subghz/scenes/subghz_scene_decode_raw.c
applications/main/subghz/scenes/subghz_scene_radio_settings.c
applications/main/subghz/scenes/subghz_scene_set_button.c
applications/main/subghz/scenes/subghz_scene_set_counter.c
applications/main/subghz/scenes/subghz_scene_set_key.c
applications/main/subghz/scenes/subghz_scene_set_seed.c
applications/main/subghz/scenes/subghz_scene_set_serial.c
applications/main/subghz/scenes/subghz_scene_show_gps.c
applications/main/subghz/scenes/subghz_scene_signal_settings.c
applications/main/subghz/scenes/subghz_scene_start.h
applications/main/subghz/subghz_extended_freq.c
applications/main/subghz/subghz_fap.c
applications/main/subghz/subghz_fap.h
applications/main/subghz/subghz_last_settings.c
applications/main/subghz/subghz_last_settings.h
applications/services/bt/bt_service/bt_settings_api.c
applications/services/desktop/desktop_keybinds.c
applications/services/desktop/desktop_keybinds.h
applications/services/desktop/desktop_keybinds_filename.h
applications/services/gui/modules/file_browser_worker_i.h
applications/services/input/input_settings.c
applications/services/input/input_settings.h
applications/services/input/input_settings_filename.h
applications/services/loader/loader_menu_storage.c
applications/services/loader/loader_menu_storage_i.h
applications/services/power/power_service/power_settings.c
applications/services/power/power_service/power_settings.h
applications/services/power/power_service/power_settings_api_i.h
applications/services/power/power_service/power_settings_filename.h
applications/settings/about/about.h
applications/settings/bt_settings_app/mock_imports/mock_bt_settings_api.c
applications/settings/desktop_settings/mock_imports/mock_desktop.c
applications/settings/desktop_settings/mock_imports/mock_desktop_keybinds.c
applications/settings/desktop_settings/mock_imports/mock_desktop_view_pin_input.c
applications/settings/desktop_settings/mock_imports/mock_furi_hal_rtc.c
applications/settings/desktop_settings/mock_imports/mock_pin_code.c
applications/settings/desktop_settings/scenes/desktop_settings_scene_keybinds_action.c
applications/settings/desktop_settings/scenes/desktop_settings_scene_keybinds_action_type.c
applications/settings/desktop_settings/scenes/desktop_settings_scene_keybinds_key.c
applications/settings/desktop_settings/scenes/desktop_settings_scene_keybinds_reset.c
applications/settings/desktop_settings/scenes/desktop_settings_scene_keybinds_type.c
applications/settings/dolphin_passport/mock_imports/mock_dolphin_state.c
applications/settings/expansion_settings_app/mock_imports/mock_expansion_settings.c
applications/settings/input_settings_app/application.fam
applications/settings/input_settings_app/input_settings_app.c
applications/settings/input_settings_app/input_settings_app.h
applications/settings/input_settings_app/mock_imports/mock_input_settings.c
applications/settings/notification_settings/mock_imports/mock_notification_app.c
applications/settings/power_settings_app/mock_imports/mock_about.c
applications/settings/power_settings_app/mock_imports/mock_power_api.c
applications/settings/storage_settings/scenes/storage_settings_scene_wipe_device.c
applications/system/findmy/README.md
applications/system/findmy/application.fam
applications/system/findmy/findmy.c
applications/system/findmy/findmy.h
applications/system/findmy/findmy_i.h
applications/system/findmy/findmy_startup.c
applications/system/findmy/findmy_state.c
applications/system/findmy/findmy_state.h
applications/system/findmy/generate_keys.py
applications/system/findmy/helpers/base64.c
applications/system/findmy/helpers/base64.h
applications/system/findmy/icons/text_10px.png
applications/system/findmy/location_icon.png
applications/system/findmy/scenes/findmy_scene.c
applications/system/findmy/scenes/findmy_scene.h
applications/system/findmy/scenes/findmy_scene_config.c
applications/system/findmy/scenes/findmy_scene_config_import.c
applications/system/findmy/scenes/findmy_scene_config_import_result.c
applications/system/findmy/scenes/findmy_scene_config_mac.c
applications/system/findmy/scenes/findmy_scene_config_packet.c
applications/system/findmy/scenes/findmy_scene_config_tagtype.c
applications/system/findmy/scenes/findmy_scene_main.c
applications/system/findmy/scenes/findmy_scenes.h
applications/system/findmy/views/findmy_main.c
applications/system/findmy/views/findmy_main.h
applications/system/hid_app/assets/BrokenButton_15x15.png
applications/system/hid_app/assets/BtnBackV_9x9.png
applications/system/hid_app/assets/BtnFrameLeft_3x18.png
applications/system/hid_app/assets/BtnFrameRight_2x18.png
applications/system/hid_app/assets/BtnLeft_9x9.png
applications/system/hid_app/assets/Hand_8x10.png
applications/system/hid_app/assets/Help_exit_64x9.png
applications/system/hid_app/assets/Help_top_64x17.png
applications/system/hid_app/assets/Hold_15x5.png
applications/system/hid_app/assets/Left_mouse_icon_9x9.png
applications/system/hid_app/assets/Like_def_11x9.png
applications/system/hid_app/assets/Mic_7x11.png
applications/system/hid_app/assets/MicrophoneCrossed_16x16.png
applications/system/hid_app/assets/MicrophonePressedBtn_16x16.png
applications/system/hid_app/assets/MicrophonePressedCrossedBtn_16x16.png
applications/system/hid_app/assets/OutCircles_70x51.png
applications/system/hid_app/assets/Pause_icon_9x9.png
applications/system/hid_app/assets/Pin_back_arrow_10x10.png
applications/system/hid_app/assets/Pressed_Button_19x19.png
applications/system/hid_app/assets/Right_mouse_icon_9x9.png
applications/system/hid_app/assets/RoundButtonUnpressed_16x16.png
applications/system/hid_app/assets/S_DOWN_31x15.png
applications/system/hid_app/assets/S_LEFT_15x31.png
applications/system/hid_app/assets/S_RIGHT_15x31.png
applications/system/hid_app/assets/S_UP_31x15.png
applications/system/hid_app/assets/for_11x5.png
applications/system/hid_app/assets/for_help_27x5.png
applications/system/hid_app/helpers/ble_hid_ext_profile.c
applications/system/hid_app/helpers/ble_hid_ext_profile.h
applications/system/hid_app/scenes/hid_scene_rename.c
applications/system/hid_app/views/hid_mouse_jiggler_stealth.c
applications/system/hid_app/views/hid_mouse_jiggler_stealth.h
applications/system/hid_app/views/hid_movie.c
applications/system/hid_app/views/hid_movie.h
applications/system/hid_app/views/hid_music_macos.c
applications/system/hid_app/views/hid_music_macos.h
applications/system/hid_app/views/hid_numpad.c
applications/system/hid_app/views/hid_numpad.h
applications/system/hid_app/views/hid_ptt.c
applications/system/hid_app/views/hid_ptt.h
applications/system/hid_app/views/hid_ptt_menu.c
applications/system/hid_app/views/hid_ptt_menu.h
applications/system/js_app/examples/apps/Scripts/Examples/array_buf_test.js
applications/system/js_app/examples/apps/Scripts/Examples/bad_uart.js
applications/system/js_app/examples/apps/Scripts/Examples/badusb_demo.js
applications/system/js_app/examples/apps/Scripts/Examples/blebeacon.js
applications/system/js_app/examples/apps/Scripts/Examples/console.js
applications/system/js_app/examples/apps/Scripts/Examples/delay.js
applications/system/js_app/examples/apps/Scripts/Examples/event_loop.js
applications/system/js_app/examples/apps/Scripts/Examples/gpio.js
applications/system/js_app/examples/apps/Scripts/Examples/gui.js
applications/system/js_app/examples/apps/Scripts/Examples/i2c.js
applications/system/js_app/examples/apps/Scripts/Examples/infrared-send.js
applications/system/js_app/examples/apps/Scripts/Examples/load.js
applications/system/js_app/examples/apps/Scripts/Examples/load_api.js
applications/system/js_app/examples/apps/Scripts/Examples/math.js
applications/system/js_app/examples/apps/Scripts/Examples/notify.js
applications/system/js_app/examples/apps/Scripts/Examples/path.js
applications/system/js_app/examples/apps/Scripts/Examples/spi.js
applications/system/js_app/examples/apps/Scripts/Examples/storage.js
applications/system/js_app/examples/apps/Scripts/Examples/stringutils.js
applications/system/js_app/examples/apps/Scripts/Examples/subghz.js
applications/system/js_app/examples/apps/Scripts/Examples/uart_echo.js
applications/system/js_app/examples/apps/Scripts/Examples/uart_echo_8e1.js
applications/system/js_app/examples/apps/Scripts/Examples/usbdisk.js
applications/system/js_app/modules/js_blebeacon.c
applications/system/js_app/modules/js_i2c.c
applications/system/js_app/modules/js_infrared/js_infrared.c
applications/system/js_app/modules/js_spi.c
applications/system/js_app/modules/js_subghz/js_subghz.c
applications/system/js_app/modules/js_subghz/radio_device_loader.c
applications/system/js_app/modules/js_subghz/radio_device_loader.h
applications/system/js_app/modules/js_usbdisk/js_usbdisk.c
applications/system/js_app/modules/js_usbdisk/mass_storage_scsi.c
applications/system/js_app/modules/js_usbdisk/mass_storage_scsi.h
applications/system/js_app/modules/js_usbdisk/mass_storage_usb.c
applications/system/js_app/modules/js_usbdisk/mass_storage_usb.h
applications/system/js_app/modules/js_vgm/ICM42688P/ICM42688P.c
applications/system/js_app/modules/js_vgm/ICM42688P/ICM42688P.h
applications/system/js_app/modules/js_vgm/ICM42688P/ICM42688P_regs.h
applications/system/js_app/modules/js_vgm/README.md
applications/system/js_app/modules/js_vgm/imu.c
applications/system/js_app/modules/js_vgm/imu.h
applications/system/js_app/modules/js_vgm/js_vgm.c
applications/system/js_app/packages/fz-sdk/blebeacon/index.d.ts
applications/system/js_app/packages/fz-sdk/i2c/index.d.ts
applications/system/js_app/packages/fz-sdk/infrared/index.d.ts
applications/system/js_app/packages/fz-sdk/spi/index.d.ts
applications/system/js_app/packages/fz-sdk/subghz/index.d.ts
applications/system/js_app/packages/fz-sdk/usbdisk/index.d.ts
applications/system/js_app/packages/fz-sdk/vgm/index.d.ts
assets/dolphin/external/L1_3d_printing_128x64/frame_0.png
assets/dolphin/external/L1_3d_printing_128x64/frame_1.png
assets/dolphin/external/L1_3d_printing_128x64/frame_10.png
assets/dolphin/external/L1_3d_printing_128x64/frame_11.png
assets/dolphin/external/L1_3d_printing_128x64/frame_12.png
assets/dolphin/external/L1_3d_printing_128x64/frame_13.png
assets/dolphin/external/L1_3d_printing_128x64/frame_14.png
assets/dolphin/external/L1_3d_printing_128x64/frame_15.png
assets/dolphin/external/L1_3d_printing_128x64/frame_16.png
assets/dolphin/external/L1_3d_printing_128x64/frame_17.png
assets/dolphin/external/L1_3d_printing_128x64/frame_18.png
assets/dolphin/external/L1_3d_printing_128x64/frame_19.png
assets/dolphin/external/L1_3d_printing_128x64/frame_2.png
assets/dolphin/external/L1_3d_printing_128x64/frame_20.png
assets/dolphin/external/L1_3d_printing_128x64/frame_21.png
assets/dolphin/external/L1_3d_printing_128x64/frame_22.png
assets/dolphin/external/L1_3d_printing_128x64/frame_23.png
assets/dolphin/external/L1_3d_printing_128x64/frame_24.png
assets/dolphin/external/L1_3d_printing_128x64/frame_25.png
assets/dolphin/external/L1_3d_printing_128x64/frame_26.png
assets/dolphin/external/L1_3d_printing_128x64/frame_27.png
assets/dolphin/external/L1_3d_printing_128x64/frame_28.png
assets/dolphin/external/L1_3d_printing_128x64/frame_29.png
assets/dolphin/external/L1_3d_printing_128x64/frame_3.png
assets/dolphin/external/L1_3d_printing_128x64/frame_30.png
assets/dolphin/external/L1_3d_printing_128x64/frame_31.png
assets/dolphin/external/L1_3d_printing_128x64/frame_32.png
assets/dolphin/external/L1_3d_printing_128x64/frame_33.png
assets/dolphin/external/L1_3d_printing_128x64/frame_34.png
assets/dolphin/external/L1_3d_printing_128x64/frame_35.png
assets/dolphin/external/L1_3d_printing_128x64/frame_36.png
assets/dolphin/external/L1_3d_printing_128x64/frame_37.png
assets/dolphin/external/L1_3d_printing_128x64/frame_38.png
assets/dolphin/external/L1_3d_printing_128x64/frame_39.png
assets/dolphin/external/L1_3d_printing_128x64/frame_4.png
assets/dolphin/external/L1_3d_printing_128x64/frame_40.png
assets/dolphin/external/L1_3d_printing_128x64/frame_41.png
assets/dolphin/external/L1_3d_printing_128x64/frame_42.png
assets/dolphin/external/L1_3d_printing_128x64/frame_43.png
assets/dolphin/external/L1_3d_printing_128x64/frame_44.png
assets/dolphin/external/L1_3d_printing_128x64/frame_45.png
assets/dolphin/external/L1_3d_printing_128x64/frame_5.png
assets/dolphin/external/L1_3d_printing_128x64/frame_6.png
assets/dolphin/external/L1_3d_printing_128x64/frame_7.png
assets/dolphin/external/L1_3d_printing_128x64/frame_8.png
assets/dolphin/external/L1_3d_printing_128x64/frame_9.png
assets/dolphin/external/L1_3d_printing_128x64/meta.txt
assets/dolphin/external/L1_Tv_128x47/frame_0.png
assets/dolphin/external/L1_Tv_128x47/frame_1.png
assets/dolphin/external/L1_Tv_128x47/frame_2.png
assets/dolphin/external/L1_Tv_128x47/frame_3.png
assets/dolphin/external/L1_Tv_128x47/frame_4.png
assets/dolphin/external/L1_Tv_128x47/frame_5.png
assets/dolphin/external/L1_Tv_128x47/frame_6.png
assets/dolphin/external/L1_Tv_128x47/frame_7.png
assets/dolphin/external/L1_Tv_128x47/meta.txt
assets/dolphin/external/L1_Wardriving_128x64/frame_0.png
assets/dolphin/external/L1_Wardriving_128x64/frame_1.png
assets/dolphin/external/L1_Wardriving_128x64/frame_2.png
assets/dolphin/external/L1_Wardriving_128x64/frame_3.png
assets/dolphin/external/L1_Wardriving_128x64/frame_4.png
assets/dolphin/external/L1_Wardriving_128x64/frame_5.png
assets/dolphin/external/L1_Wardriving_128x64/frame_6.png
assets/dolphin/external/L1_Wardriving_128x64/meta.txt
assets/dolphin/internal/L1_AnimationError_128x64/frame_0.png
assets/dolphin/internal/L1_AnimationError_128x64/meta.txt
assets/icons/Animations/Levelup_128x64/frame_00.png
assets/icons/Animations/Levelup_128x64/frame_01.png
assets/icons/Animations/Levelup_128x64/frame_02.png
assets/icons/Animations/Levelup_128x64/frame_03.png
assets/icons/Animations/Levelup_128x64/frame_04.png
assets/icons/Animations/Levelup_128x64/frame_05.png
assets/icons/Animations/Levelup_128x64/frame_06.png
assets/icons/Animations/Levelup_128x64/frame_07.png
assets/icons/Animations/Levelup_128x64/frame_08.png
assets/icons/Animations/Levelup_128x64/frame_09.png
assets/icons/Animations/Levelup_128x64/frame_10.png
assets/icons/Animations/Levelup_128x64/frame_rate
assets/icons/Archive/Apps_10px.png
assets/icons/Archive/floppydisk_10px.png
assets/icons/Archive/ir_scope_10px.png
assets/icons/Archive/mag_card_10px.png
assets/icons/Archive/protopirate_10px.png
assets/icons/Archive/search_10px.png
assets/icons/Archive/subplaylist_10px.png
assets/icons/Archive/subrem_10px.png
assets/icons/Archive/xremote_10px.png
assets/icons/ControlCenter/CC_Bluetooth_16x16.png
assets/icons/ControlCenter/CC_DarkMode_16x16.png
assets/icons/ControlCenter/CC_LefthandedMode_16x16.png
assets/icons/ControlCenter/CC_Lock_16x16.png
assets/icons/ControlCenter/CC_Momentum_16x16.png
assets/icons/ControlCenter/CC_Settings_16x16.png
assets/icons/Infrared/blue_19x20.png
assets/icons/Infrared/blue_hover_19x20.png
assets/icons/Infrared/brightness_text_40x5.png
assets/icons/Infrared/color_text_24x5.png
assets/icons/Infrared/eject_19x20.png
assets/icons/Infrared/eject_hover_19x20.png
assets/icons/Infrared/eject_text_19x5.png
assets/icons/Infrared/exit_19x20.png
assets/icons/Infrared/exit_hover_19x20.png
assets/icons/Infrared/exit_text_18x5.png
assets/icons/Infrared/fast_backward_19x20.png
assets/icons/Infrared/fast_backward_hover_19x20.png
assets/icons/Infrared/fast_backward_text_19x6.png
assets/icons/Infrared/fast_f_19x20.png
assets/icons/Infrared/fast_f_hover_19x20.png
assets/icons/Infrared/fast_f_text_19x6.png
assets/icons/Infrared/green_19x20.png
assets/icons/Infrared/green_hover_19x20.png
assets/icons/Infrared/input_19x20.png
assets/icons/Infrared/input_hover_19x20.png
assets/icons/Infrared/input_text_24x5.png
assets/icons/Infrared/menu_text_20x5.png
assets/icons/Infrared/minus_19x20.png
assets/icons/Infrared/minus_hover_19x20.png
assets/icons/Infrared/mode_19x20.png
assets/icons/Infrared/mode_hover_19x20.png
assets/icons/Infrared/mode_text_20x5.png
assets/icons/Infrared/ok_19x20.png
assets/icons/Infrared/ok_hover_19x20.png
assets/icons/Infrared/ok_text_19x5.png
assets/icons/Infrared/on_text_9x5.png
assets/icons/Infrared/plus_19x20.png
assets/icons/Infrared/plus_hover_19x20.png
assets/icons/Infrared/red_19x20.png
assets/icons/Infrared/red_hover_19x20.png
assets/icons/Infrared/rotate_19x20.png
assets/icons/Infrared/rotate_hover_19x20.png
assets/icons/Infrared/rotate_text_24x5.png
assets/icons/Infrared/speed_text_30x30.png
assets/icons/Infrared/stop_19x20.png
assets/icons/Infrared/stop_hover_19x20.png
assets/icons/Infrared/stop_text_19x5.png
assets/icons/Infrared/subtitle_19x20.png
assets/icons/Infrared/subtitle_hover_19x20.png
assets/icons/Infrared/subtitle_text_19x5.png
assets/icons/Infrared/timer_19x20.png
assets/icons/Infrared/timer_hover_19x20.png
assets/icons/Infrared/timer_text_23x5.png
assets/icons/Infrared/white_19x20.png
assets/icons/Infrared/white_hover_19x20.png
assets/icons/Interface/Lockscreen.png
assets/icons/Keyboard/KeyBackspaceSelected_17x11.png
assets/icons/Keyboard/KeyBackspace_17x11.png
assets/icons/Keyboard/KeyKeyboardSelected_10x11.png
assets/icons/Keyboard/KeyKeyboard_10x11.png
assets/icons/Keyboard/KeySaveBlockedSelected_22x11.png
assets/icons/Keyboard/KeySaveBlocked_22x11.png
assets/icons/Keyboard/KeySaveSelected_22x11.png
assets/icons/Keyboard/KeySave_22x11.png
assets/icons/MainMenu/Momentum_14/frame_01.png
assets/icons/MainMenu/Momentum_14/frame_02.png
assets/icons/MainMenu/Momentum_14/frame_03.png
assets/icons/MainMenu/Momentum_14/frame_04.png
assets/icons/MainMenu/Momentum_14/frame_05.png
assets/icons/MainMenu/Momentum_14/frame_06.png
assets/icons/MainMenu/Momentum_14/frame_07.png
assets/icons/MainMenu/Momentum_14/frame_08.png
assets/icons/MainMenu/Momentum_14/frame_09.png
assets/icons/MainMenu/Momentum_14/frame_10.png
assets/icons/MainMenu/Momentum_14/frame_11.png
assets/icons/MainMenu/Momentum_14/frame_rate
assets/icons/Passport/passport_128x64.png
assets/icons/Passport/passport_bad_46x49.png
assets/icons/Passport/passport_happy_46x49.png
assets/icons/Passport/passport_okay_46x49.png
assets/icons/RFID/RFIDSmallChip_14x14.png
assets/icons/StatusBar/Battery_25x8.png
assets/icons/StatusBar/Charging_lightning_9x10.png
assets/icons/StatusBar/Charging_lightning_mask_9x10.png
assets/icons/SubGhz/Cos_9x7.png
assets/icons/SubGhz/Dynamic_9x7.png
assets/icons/SubGhz/Fishing_123x52.png
assets/icons/SubGhz/Raw_9x7.png
assets/icons/SubGhz/Sats_6x9.png
assets/icons/SubGhz/Scanning_123x52.png
assets/icons/SubGhz/Static_9x7.png
assets/icons/SubGhz/Weather_7x8.png
assets/icons/Update/Updating_Logo_62x15.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_0.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_1.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_10.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_11.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_12.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_13.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_14.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_15.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_16.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_17.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_18.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_19.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_2.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_20.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_21.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_22.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_23.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_24.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_25.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_26.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_27.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_28.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_29.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_3.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_30.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_31.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_32.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_33.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_34.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_35.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_36.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_37.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_38.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_39.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_4.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_40.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_41.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_42.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_5.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_6.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_7.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_8.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/frame_9.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum1_128x64/meta.txt
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_0.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_1.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_10.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_11.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_12.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_13.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_14.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_15.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_16.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_17.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_18.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_19.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_2.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_20.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_21.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_22.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_23.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_24.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_25.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_26.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_27.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_28.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_29.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_3.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_30.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_31.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_32.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_33.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_34.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_4.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_5.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_6.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_7.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_8.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/frame_9.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum2_128x64/meta.txt
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_0.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_1.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_10.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_11.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_12.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_13.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_14.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_15.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_16.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_17.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_18.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_19.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_2.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_20.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_21.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_22.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_23.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_24.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_25.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_26.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_27.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_28.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_29.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_3.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_30.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_31.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_32.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_33.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_34.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_35.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_36.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_37.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_38.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_39.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_4.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_5.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_6.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_7.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_8.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/frame_9.png
assets/packs/Momentum/Anims/Kuronons_CFW_Momentum3_128x64/meta.txt
assets/packs/Momentum/Anims/manifest.txt
assets/packs/Momentum/Icons/Animations/Levelup_128x64/frame_00.png
assets/packs/Momentum/Icons/Animations/Levelup_128x64/frame_01.png
assets/packs/Momentum/Icons/Animations/Levelup_128x64/frame_02.png
assets/packs/Momentum/Icons/Animations/Levelup_128x64/frame_03.png
assets/packs/Momentum/Icons/Animations/Levelup_128x64/frame_04.png
assets/packs/Momentum/Icons/Animations/Levelup_128x64/frame_05.png
assets/packs/Momentum/Icons/Animations/Levelup_128x64/frame_06.png
assets/packs/Momentum/Icons/Animations/Levelup_128x64/frame_07.png
assets/packs/Momentum/Icons/Animations/Levelup_128x64/frame_08.png
assets/packs/Momentum/Icons/Animations/Levelup_128x64/frame_09.png
assets/packs/Momentum/Icons/Animations/Levelup_128x64/frame_10.png
assets/packs/Momentum/Icons/Animations/Levelup_128x64/frame_rate
assets/packs/Momentum/Icons/BLE/BLE_Pairing_128x64.png
assets/packs/Momentum/Icons/Dolphin/DolphinDone_80x58.png
assets/packs/Momentum/Icons/Dolphin/DolphinMafia_119x62.png
assets/packs/Momentum/Icons/Dolphin/DolphinReadingSuccess_59x63.png
assets/packs/Momentum/Icons/Dolphin/DolphinSaved_92x58.png
assets/packs/Momentum/Icons/Dolphin/DolphinSuccess_91x55.png
assets/packs/Momentum/Icons/Dolphin/DolphinWait_59x54.png
assets/packs/Momentum/Icons/Dolphin/WarningDolphinFlip_45x42.png
assets/packs/Momentum/Icons/Dolphin/WarningDolphin_45x42.png
assets/packs/Momentum/Icons/Infrared/InfraredLearnShort_128x31.png
assets/packs/Momentum/Icons/Interface/Lockscreen.png
assets/packs/Momentum/Icons/NFC/NFC_dolphin_emulation_51x64.png
assets/packs/Momentum/Icons/Passport/passport_bad_46x49.png
assets/packs/Momentum/Icons/Passport/passport_happy_46x49.png
assets/packs/Momentum/Icons/Passport/passport_okay_46x49.png
assets/packs/Momentum/Icons/RFID/RFIDDolphinReceive_97x61.png
assets/packs/Momentum/Icons/RFID/RFIDDolphinSend_97x61.png
assets/packs/Momentum/Icons/Settings/dolph_cry_49x54.png
assets/packs/Momentum/Icons/SubGhz/Fishing_123x52.png
assets/packs/Momentum/Icons/SubGhz/Scanning_123x52.png
assets/packs/Momentum/Icons/U2F/Auth_62x31.png
assets/packs/Momentum/Icons/U2F/Connect_me_62x31.png
assets/packs/Momentum/Icons/U2F/Connected_62x31.png
assets/packs/Momentum/Icons/U2F/Error_62x31.png
assets/packs/Momentum/Icons/iButton/iButtonDolphinVerySuccess_92x55.png
assets/packs/ReadMe.md
assets/packs/WatchDogs/Anims/BOTTY_CALL/frame_0.png
assets/packs/WatchDogs/Anims/BOTTY_CALL/frame_1.png
assets/packs/WatchDogs/Anims/BOTTY_CALL/frame_2.png
assets/packs/WatchDogs/Anims/BOTTY_CALL/meta.txt
assets/packs/WatchDogs/Anims/DEDSEC_AD/frame_0.png
assets/packs/WatchDogs/Anims/DEDSEC_AD/frame_1.png
assets/packs/WatchDogs/Anims/DEDSEC_AD/frame_10.png
assets/packs/WatchDogs/Anims/DEDSEC_AD/frame_11.png
assets/packs/WatchDogs/Anims/DEDSEC_AD/frame_12.png
assets/packs/WatchDogs/Anims/DEDSEC_AD/frame_13.png
assets/packs/WatchDogs/Anims/DEDSEC_AD/frame_14.png
assets/packs/WatchDogs/Anims/DEDSEC_AD/frame_15.png
assets/packs/WatchDogs/Anims/DEDSEC_AD/frame_16.png
assets/packs/WatchDogs/Anims/DEDSEC_AD/frame_2.png
assets/packs/WatchDogs/Anims/DEDSEC_AD/frame_3.png
assets/packs/WatchDogs/Anims/DEDSEC_AD/frame_4.png
assets/packs/WatchDogs/Anims/DEDSEC_AD/frame_5.png
assets/packs/WatchDogs/Anims/DEDSEC_AD/frame_6.png
assets/packs/WatchDogs/Anims/DEDSEC_AD/frame_7.png
assets/packs/WatchDogs/Anims/DEDSEC_AD/frame_8.png
assets/packs/WatchDogs/Anims/DEDSEC_AD/frame_9.png
assets/packs/WatchDogs/Anims/DEDSEC_AD/meta.txt
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_0.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_1.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_10.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_11.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_12.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_13.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_14.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_15.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_16.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_17.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_18.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_19.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_2.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_20.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_21.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_22.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_23.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_24.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_25.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_26.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_27.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_28.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_29.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_3.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_30.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_31.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_32.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_33.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_34.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_35.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_36.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_37.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_38.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_39.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_4.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_40.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_41.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_5.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_6.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_7.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_8.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/frame_9.png
assets/packs/WatchDogs/Anims/DEDSEC_ANIM/meta.txt
assets/packs/WatchDogs/Anims/DEDSEC_ASCII/frame_0.png
assets/packs/WatchDogs/Anims/DEDSEC_ASCII/frame_1.png
assets/packs/WatchDogs/Anims/DEDSEC_ASCII/frame_10.png
assets/packs/WatchDogs/Anims/DEDSEC_ASCII/frame_2.png
assets/packs/WatchDogs/Anims/DEDSEC_ASCII/frame_3.png
assets/packs/WatchDogs/Anims/DEDSEC_ASCII/frame_4.png
assets/packs/WatchDogs/Anims/DEDSEC_ASCII/frame_5.png
assets/packs/WatchDogs/Anims/DEDSEC_ASCII/frame_6.png
assets/packs/WatchDogs/Anims/DEDSEC_ASCII/frame_7.png
assets/packs/WatchDogs/Anims/DEDSEC_ASCII/frame_8.png
assets/packs/WatchDogs/Anims/DEDSEC_ASCII/frame_9.png
assets/packs/WatchDogs/Anims/DEDSEC_ASCII/meta.txt
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_0.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_1.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_10.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_11.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_12.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_13.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_14.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_15.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_16.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_17.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_18.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_19.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_2.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_20.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_21.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_22.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_23.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_24.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_25.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_26.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_27.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_28.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_29.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_3.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_30.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_31.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_4.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_5.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_6.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_7.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_8.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/frame_9.png
assets/packs/WatchDogs/Anims/DEDSEC_LOGO/meta.txt
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_0.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_1.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_10.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_11.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_12.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_13.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_14.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_15.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_16.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_17.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_18.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_19.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_2.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_20.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_3.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_4.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_5.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_6.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_7.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_8.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/frame_9.png
assets/packs/WatchDogs/Anims/DEDSEC_OLD/meta.txt
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_0.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_1.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_10.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_11.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_12.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_13.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_14.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_15.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_16.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_17.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_2.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_3.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_4.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_5.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_6.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_7.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_8.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/frame_9.png
assets/packs/WatchDogs/Anims/DEDSEC_TALK/meta.txt
assets/packs/WatchDogs/Anims/DEDSEC_WAVE/frame_0.png
assets/packs/WatchDogs/Anims/DEDSEC_WAVE/frame_1.png
assets/packs/WatchDogs/Anims/DEDSEC_WAVE/frame_10.png
assets/packs/WatchDogs/Anims/DEDSEC_WAVE/frame_11.png
assets/packs/WatchDogs/Anims/DEDSEC_WAVE/frame_12.png
assets/packs/WatchDogs/Anims/DEDSEC_WAVE/frame_13.png
assets/packs/WatchDogs/Anims/DEDSEC_WAVE/frame_14.png
assets/packs/WatchDogs/Anims/DEDSEC_WAVE/frame_15.png
assets/packs/WatchDogs/Anims/DEDSEC_WAVE/frame_2.png
assets/packs/WatchDogs/Anims/DEDSEC_WAVE/frame_3.png
assets/packs/WatchDogs/Anims/DEDSEC_WAVE/frame_4.png
assets/packs/WatchDogs/Anims/DEDSEC_WAVE/frame_5.png
assets/packs/WatchDogs/Anims/DEDSEC_WAVE/frame_6.png
assets/packs/WatchDogs/Anims/DEDSEC_WAVE/frame_7.png
assets/packs/WatchDogs/Anims/DEDSEC_WAVE/frame_8.png
assets/packs/WatchDogs/Anims/DEDSEC_WAVE/frame_9.png
assets/packs/WatchDogs/Anims/DEDSEC_WAVE/meta.txt
assets/packs/WatchDogs/Anims/FINGER/frame_0.png
assets/packs/WatchDogs/Anims/FINGER/frame_1.png
assets/packs/WatchDogs/Anims/FINGER/frame_2.png
assets/packs/WatchDogs/Anims/FINGER/frame_3.png
assets/packs/WatchDogs/Anims/FINGER/meta.txt
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_0.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_1.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_10.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_11.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_12.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_13.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_14.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_15.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_16.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_17.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_18.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_19.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_2.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_20.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_21.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_3.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_4.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_5.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_6.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_7.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_8.png
assets/packs/WatchDogs/Anims/GUNS_CAR/frame_9.png
assets/packs/WatchDogs/Anims/GUNS_CAR/meta.txt
assets/packs/WatchDogs/Anims/HANDS/frame_0.png
assets/packs/WatchDogs/Anims/HANDS/frame_1.png
assets/packs/WatchDogs/Anims/HANDS/frame_2.png
assets/packs/WatchDogs/Anims/HANDS/frame_3.png
assets/packs/WatchDogs/Anims/HANDS/frame_4.png
assets/packs/WatchDogs/Anims/HANDS/frame_5.png
assets/packs/WatchDogs/Anims/HANDS/frame_6.png
assets/packs/WatchDogs/Anims/HANDS/meta.txt
assets/packs/WatchDogs/Anims/JOIN_US/frame_0.png
assets/packs/WatchDogs/Anims/JOIN_US/frame_1.png
assets/packs/WatchDogs/Anims/JOIN_US/meta.txt
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_0.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_1.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_10.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_11.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_12.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_13.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_14.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_15.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_16.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_17.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_18.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_19.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_2.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_20.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_21.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_22.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_23.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_24.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_25.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_26.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_27.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_28.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_29.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_3.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_30.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_4.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_5.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_6.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_7.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_8.png
assets/packs/WatchDogs/Anims/LOGO_WD2/frame_9.png
assets/packs/WatchDogs/Anims/LOGO_WD2/meta.txt
assets/packs/WatchDogs/Anims/MARCUS/frame_0.png
assets/packs/WatchDogs/Anims/MARCUS/frame_1.png
assets/packs/WatchDogs/Anims/MARCUS/frame_10.png
assets/packs/WatchDogs/Anims/MARCUS/frame_11.png
assets/packs/WatchDogs/Anims/MARCUS/frame_12.png
assets/packs/WatchDogs/Anims/MARCUS/frame_13.png
assets/packs/WatchDogs/Anims/MARCUS/frame_14.png
assets/packs/WatchDogs/Anims/MARCUS/frame_15.png
assets/packs/WatchDogs/Anims/MARCUS/frame_16.png
assets/packs/WatchDogs/Anims/MARCUS/frame_17.png
assets/packs/WatchDogs/Anims/MARCUS/frame_18.png
assets/packs/WatchDogs/Anims/MARCUS/frame_19.png
assets/packs/WatchDogs/Anims/MARCUS/frame_2.png
assets/packs/WatchDogs/Anims/MARCUS/frame_20.png
assets/packs/WatchDogs/Anims/MARCUS/frame_21.png
assets/packs/WatchDogs/Anims/MARCUS/frame_22.png
assets/packs/WatchDogs/Anims/MARCUS/frame_23.png
assets/packs/WatchDogs/Anims/MARCUS/frame_3.png
assets/packs/WatchDogs/Anims/MARCUS/frame_4.png
assets/packs/WatchDogs/Anims/MARCUS/frame_5.png
assets/packs/WatchDogs/Anims/MARCUS/frame_6.png
assets/packs/WatchDogs/Anims/MARCUS/frame_7.png
assets/packs/WatchDogs/Anims/MARCUS/frame_8.png
assets/packs/WatchDogs/Anims/MARCUS/frame_9.png
assets/packs/WatchDogs/Anims/MARCUS/meta.txt
assets/packs/WatchDogs/Anims/MUMMY/frame_0.png
assets/packs/WatchDogs/Anims/MUMMY/frame_1.png
assets/packs/WatchDogs/Anims/MUMMY/frame_2.png
assets/packs/WatchDogs/Anims/MUMMY/frame_3.png
assets/packs/WatchDogs/Anims/MUMMY/meta.txt
assets/packs/WatchDogs/Anims/REAPER/frame_0.png
assets/packs/WatchDogs/Anims/REAPER/frame_1.png
assets/packs/WatchDogs/Anims/REAPER/frame_10.png
assets/packs/WatchDogs/Anims/REAPER/frame_11.png
assets/packs/WatchDogs/Anims/REAPER/frame_12.png
assets/packs/WatchDogs/Anims/REAPER/frame_13.png
assets/packs/WatchDogs/Anims/REAPER/frame_14.png
assets/packs/WatchDogs/Anims/REAPER/frame_15.png
assets/packs/WatchDogs/Anims/REAPER/frame_16.png
assets/packs/WatchDogs/Anims/REAPER/frame_17.png
assets/packs/WatchDogs/Anims/REAPER/frame_18.png
assets/packs/WatchDogs/Anims/REAPER/frame_19.png
assets/packs/WatchDogs/Anims/REAPER/frame_2.png
assets/packs/WatchDogs/Anims/REAPER/frame_20.png
assets/packs/WatchDogs/Anims/REAPER/frame_21.png
assets/packs/WatchDogs/Anims/REAPER/frame_22.png
assets/packs/WatchDogs/Anims/REAPER/frame_23.png
assets/packs/WatchDogs/Anims/REAPER/frame_24.png
assets/packs/WatchDogs/Anims/REAPER/frame_25.png
assets/packs/WatchDogs/Anims/REAPER/frame_26.png
assets/packs/WatchDogs/Anims/REAPER/frame_27.png
assets/packs/WatchDogs/Anims/REAPER/frame_28.png
assets/packs/WatchDogs/Anims/REAPER/frame_29.png
assets/packs/WatchDogs/Anims/REAPER/frame_3.png
assets/packs/WatchDogs/Anims/REAPER/frame_30.png
assets/packs/WatchDogs/Anims/REAPER/frame_31.png
assets/packs/WatchDogs/Anims/REAPER/frame_4.png
assets/packs/WatchDogs/Anims/REAPER/frame_5.png
assets/packs/WatchDogs/Anims/REAPER/frame_6.png
assets/packs/WatchDogs/Anims/REAPER/frame_7.png
assets/packs/WatchDogs/Anims/REAPER/frame_8.png
assets/packs/WatchDogs/Anims/REAPER/frame_9.png
assets/packs/WatchDogs/Anims/REAPER/meta.txt
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_0.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_1.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_10.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_11.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_12.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_13.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_14.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_15.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_16.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_17.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_18.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_19.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_2.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_20.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_21.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_22.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_23.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_24.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_25.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_26.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_27.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_28.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_29.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_3.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_30.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_31.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_32.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_33.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_34.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_35.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_36.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_37.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_38.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_39.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_4.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_40.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_41.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_5.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_6.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_7.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_8.png
assets/packs/WatchDogs/Anims/REAPER_ALT/frame_9.png
assets/packs/WatchDogs/Anims/REAPER_ALT/meta.txt
assets/packs/WatchDogs/Anims/SKULL/frame_0.png
assets/packs/WatchDogs/Anims/SKULL/frame_1.png
assets/packs/WatchDogs/Anims/SKULL/frame_10.png
assets/packs/WatchDogs/Anims/SKULL/frame_11.png
assets/packs/WatchDogs/Anims/SKULL/frame_12.png
assets/packs/WatchDogs/Anims/SKULL/frame_13.png
assets/packs/WatchDogs/Anims/SKULL/frame_14.png
assets/packs/WatchDogs/Anims/SKULL/frame_15.png
assets/packs/WatchDogs/Anims/SKULL/frame_16.png
assets/packs/WatchDogs/Anims/SKULL/frame_17.png
assets/packs/WatchDogs/Anims/SKULL/frame_18.png
assets/packs/WatchDogs/Anims/SKULL/frame_19.png
assets/packs/WatchDogs/Anims/SKULL/frame_2.png
assets/packs/WatchDogs/Anims/SKULL/frame_20.png
assets/packs/WatchDogs/Anims/SKULL/frame_21.png
assets/packs/WatchDogs/Anims/SKULL/frame_22.png
assets/packs/WatchDogs/Anims/SKULL/frame_23.png
assets/packs/WatchDogs/Anims/SKULL/frame_24.png
assets/packs/WatchDogs/Anims/SKULL/frame_25.png
assets/packs/WatchDogs/Anims/SKULL/frame_26.png
assets/packs/WatchDogs/Anims/SKULL/frame_3.png
assets/packs/WatchDogs/Anims/SKULL/frame_4.png
assets/packs/WatchDogs/Anims/SKULL/frame_5.png
assets/packs/WatchDogs/Anims/SKULL/frame_6.png
assets/packs/WatchDogs/Anims/SKULL/frame_7.png
assets/packs/WatchDogs/Anims/SKULL/frame_8.png
assets/packs/WatchDogs/Anims/SKULL/frame_9.png
assets/packs/WatchDogs/Anims/SKULL/meta.txt
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_0.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_1.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_10.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_11.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_12.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_13.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_14.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_15.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_16.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_17.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_18.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_2.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_3.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_4.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_5.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_6.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_7.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_8.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/frame_9.png
assets/packs/WatchDogs/Anims/SKULL_SPIN/meta.txt
assets/packs/WatchDogs/Anims/SPIRAL/frame_0.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_1.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_10.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_11.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_12.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_13.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_14.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_15.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_16.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_17.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_18.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_19.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_2.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_20.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_21.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_22.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_23.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_3.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_4.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_5.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_6.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_7.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_8.png
assets/packs/WatchDogs/Anims/SPIRAL/frame_9.png
assets/packs/WatchDogs/Anims/SPIRAL/meta.txt
assets/packs/WatchDogs/Anims/manifest.txt
assets/packs/WatchDogs/Fonts/Keyboard.c
assets/packs/WatchDogs/Fonts/Primary.c
assets/packs/WatchDogs/Fonts/Secondary.c
assets/packs/WatchDogs/Icons/Dolphin/DolphinSuccess_91x55.png
assets/packs/WatchDogs/Icons/NFC/NFC_dolphin_emulation_51x64.png
assets/packs/WatchDogs/Icons/Passport/passport_128x64.png
assets/packs/WatchDogs/Icons/Passport/passport_bad_46x49.png
assets/packs/WatchDogs/Icons/Passport/passport_happy_46x49.png
assets/packs/WatchDogs/Icons/Passport/passport_okay_46x49.png
assets/packs/WatchDogs/Icons/RFID/RFIDDolphinReceive_97x61.png
assets/packs/WatchDogs/Icons/RFID/RFIDDolphinSend_97x61.png
assets/packs/WatchDogs/Icons/SubGhz/Fishing_123x52.png
assets/packs/WatchDogs/Icons/SubGhz/Scanning_123x52.png
assets/slideshow/firstboot/frame_00.png
assets/slideshow/firstboot/frame_01.png
assets/slideshow/firstboot/frame_02.png
assets/slideshow/firstboot/frame_03.png
assets/slideshow/firstboot/frame_04.png
assets/slideshow/firstboot/frame_05.png
assets/slideshow/firstboot/frame_06.png
documentation/CustomFlipperName.md
documentation/InfraredCaptures.md
documentation/MultiConverter.md
documentation/NRF24.md
documentation/SentrySafe.md
documentation/SubGHzBypass&Extend.md
documentation/SubGHzCounterMode.md
documentation/SubGHzRemotePlugin.md
documentation/SubGHzRemoteProg.md
documentation/SubGHzSettings.md
documentation/SubGHzSupportedSystems.md
documentation/file_formats/AssetPacks.md
documentation/js/ReadMe.md
lib/drivers/SK6805.c
lib/drivers/SK6805.h
lib/drivers/rgb_backlight.c
lib/drivers/rgb_backlight.h
lib/drivers/rgb_backlight_filename.h
lib/ibutton/protocols/dallas/protocol_ds1420.c
lib/ibutton/protocols/dallas/protocol_ds1420.h
lib/lfrfid/protocols/protocol_indala224.c
lib/lfrfid/protocols/protocol_indala224.h
lib/lfrfid/protocols/protocol_insta_fob.c
lib/lfrfid/protocols/protocol_insta_fob.h
lib/momentum/SConscript
lib/momentum/asset_packs.c
lib/momentum/asset_packs.h
lib/momentum/asset_packs_i.h
lib/momentum/momentum.h
lib/momentum/namespoof.c
lib/momentum/namespoof.h
lib/momentum/settings.c
lib/momentum/settings.h
lib/momentum/settings_i.h
lib/nfc/helpers/nxp_native_command.c
lib/nfc/helpers/nxp_native_command.h
lib/nfc/helpers/nxp_native_command_mode.h
lib/nfc/protocols/emv/emv.c
lib/nfc/protocols/emv/emv.h
lib/nfc/protocols/emv/emv_poller.c
lib/nfc/protocols/emv/emv_poller.h
lib/nfc/protocols/emv/emv_poller_defs.h
lib/nfc/protocols/emv/emv_poller_i.c
lib/nfc/protocols/emv/emv_poller_i.h
lib/nfc/protocols/ntag4xx/ntag4xx.c
lib/nfc/protocols/ntag4xx/ntag4xx.h
lib/nfc/protocols/ntag4xx/ntag4xx_i.c
lib/nfc/protocols/ntag4xx/ntag4xx_i.h
lib/nfc/protocols/ntag4xx/ntag4xx_poller.c
lib/nfc/protocols/ntag4xx/ntag4xx_poller.h
lib/nfc/protocols/ntag4xx/ntag4xx_poller_defs.h
lib/nfc/protocols/ntag4xx/ntag4xx_poller_i.c
lib/nfc/protocols/ntag4xx/ntag4xx_poller_i.h
lib/nfc/protocols/type_4_tag/type_4_tag.c
lib/nfc/protocols/type_4_tag/type_4_tag.h
lib/nfc/protocols/type_4_tag/type_4_tag_i.c
lib/nfc/protocols/type_4_tag/type_4_tag_i.h
lib/nfc/protocols/type_4_tag/type_4_tag_listener.c
lib/nfc/protocols/type_4_tag/type_4_tag_listener.h
lib/nfc/protocols/type_4_tag/type_4_tag_listener_defs.h
lib/nfc/protocols/type_4_tag/type_4_tag_listener_i.c
lib/nfc/protocols/type_4_tag/type_4_tag_listener_i.h
lib/nfc/protocols/type_4_tag/type_4_tag_poller.c
lib/nfc/protocols/type_4_tag/type_4_tag_poller.h
lib/nfc/protocols/type_4_tag/type_4_tag_poller_defs.h
lib/nfc/protocols/type_4_tag/type_4_tag_poller_i.c
lib/nfc/protocols/type_4_tag/type_4_tag_poller_i.h
lib/subghz/blocks/custom_btn.c
lib/subghz/blocks/custom_btn.h
lib/subghz/blocks/custom_btn_i.h
lib/subghz/blocks/generic_i.h
lib/subghz/devices/tx.h
lib/subghz/protocols/acurite_592txr.c
lib/subghz/protocols/acurite_592txr.h
lib/subghz/protocols/acurite_5n1.c
lib/subghz/protocols/acurite_5n1.h
lib/subghz/protocols/acurite_606tx.c
lib/subghz/protocols/acurite_606tx.h
lib/subghz/protocols/acurite_609txc.c
lib/subghz/protocols/acurite_609txc.h
lib/subghz/protocols/acurite_986.c
lib/subghz/protocols/acurite_986.h
lib/subghz/protocols/aes_common.c
lib/subghz/protocols/aes_common.h
lib/subghz/protocols/allstar_firefly.c
lib/subghz/protocols/allstar_firefly.h
lib/subghz/protocols/ambient_weather.c
lib/subghz/protocols/ambient_weather.h
lib/subghz/protocols/auriol_ahfl.c
lib/subghz/protocols/auriol_ahfl.h
lib/subghz/protocols/auriol_hg0601a.c
lib/subghz/protocols/auriol_hg0601a.h
lib/subghz/protocols/beninca_arc.c
lib/subghz/protocols/beninca_arc.h
lib/subghz/protocols/bresser_3ch.c
lib/subghz/protocols/bresser_3ch.h
lib/subghz/protocols/ditec_gol4.c
lib/subghz/protocols/ditec_gol4.h
lib/subghz/protocols/emos_e601x.c
lib/subghz/protocols/emos_e601x.h
lib/subghz/protocols/gt_wt_02.c
lib/subghz/protocols/gt_wt_02.h
lib/subghz/protocols/gt_wt_03.c
lib/subghz/protocols/gt_wt_03.h
lib/subghz/protocols/honeywell.c
lib/subghz/protocols/honeywell.h
lib/subghz/protocols/hormann_bisecur.c
lib/subghz/protocols/hormann_bisecur.h
lib/subghz/protocols/infactory.c
lib/subghz/protocols/infactory.h
lib/subghz/protocols/jarolift.c
lib/subghz/protocols/jarolift.h
lib/subghz/protocols/kedsum_th.c
lib/subghz/protocols/kedsum_th.h
lib/subghz/protocols/keyfinder.c
lib/subghz/protocols/keyfinder.h
lib/subghz/protocols/lacrosse_tx.c
lib/subghz/protocols/lacrosse_tx.h
lib/subghz/protocols/lacrosse_tx141thbv2.c
lib/subghz/protocols/lacrosse_tx141thbv2.h
lib/subghz/protocols/nexus_th.c
lib/subghz/protocols/nexus_th.h
lib/subghz/protocols/nord_ice.c
lib/subghz/protocols/nord_ice.h
lib/subghz/protocols/oregon2.c
lib/subghz/protocols/oregon2.h
lib/subghz/protocols/oregon3.c
lib/subghz/protocols/oregon3.h
lib/subghz/protocols/oregon_v1.c
lib/subghz/protocols/oregon_v1.h
lib/subghz/protocols/pcsg_generic.c
lib/subghz/protocols/pcsg_generic.h
lib/subghz/protocols/pocsag.c
lib/subghz/protocols/pocsag.h
lib/subghz/protocols/schrader_gg4.c
lib/subghz/protocols/schrader_gg4.h
lib/subghz/protocols/solight_te44.c
lib/subghz/protocols/solight_te44.h
lib/subghz/protocols/thermopro_tx4.c
lib/subghz/protocols/thermopro_tx4.h
lib/subghz/protocols/tpms_generic.c
lib/subghz/protocols/tpms_generic.h
lib/subghz/protocols/treadmill37.c
lib/subghz/protocols/treadmill37.h
lib/subghz/protocols/tx_8300.c
lib/subghz/protocols/tx_8300.h
lib/subghz/protocols/vauno_en8822c.c
lib/subghz/protocols/vauno_en8822c.h
lib/subghz/protocols/wendox_w6726.c
lib/subghz/protocols/wendox_w6726.h
lib/subghz/protocols/ws_generic.c
lib/subghz/protocols/ws_generic.h
lib/subghz/protocols/x10.c
lib/subghz/protocols/x10.h
lib/subghz/subghz_keystore_i.h
lib/toolbox/colors.c
lib/toolbox/colors.h
lib/toolbox/run_parallel.c
lib/toolbox/run_parallel.h
lib/uzlib
lib/uzlib.scons
scripts/User/FlipperPlaylist.py
scripts/User/ReadMe.md
scripts/User/decode.py
scripts/User/encode.py
scripts/User/icondecode.py
scripts/User/iconencode.py
scripts/asset_packer.py
scripts/check_unused_icons.py
scripts/enable_debug.py
scripts/fix_ir_universals.py
scripts/fix_mfc_dict.py
targets/f7/furi_hal/furi_hal_region_i.h
targets/f7/furi_hal/furi_hal_subghz_i.h
targets/f7/furi_hal/furi_hal_usb_ccid.c
targets/furi_hal_include/furi_hal_usb_ccid.h
```

</details>

### Shared paths with different file content — 805

These are all shared regular-file paths whose Git blob IDs differ. A changed blob can be source, configuration, documentation, text assets, or binary assets; membership in this list establishes a byte-level difference, not the semantic importance of that difference.

<details>
<summary>Show all 805 shared paths with different content</summary>

```text
.gitattributes
.github/CODEOWNERS
.github/ISSUE_TEMPLATE/01_bug_report.yml
.github/ISSUE_TEMPLATE/config.yml
.github/pull_request_template.md
.github/workflows/build.yml
.gitignore
.gitmodules
.vscode/example/launch.json
.vscode/example/tasks.json
CONTRIBUTING.md
ReadMe.md
SConstruct
applications/ReadMe.md
applications/debug/accessor/accessor_app.cpp
applications/debug/battery_test_app/application.fam
applications/debug/file_browser_test/scenes/file_browser_scene_start.c
applications/debug/subghz_test/views/subghz_test_carrier.c
applications/debug/subghz_test/views/subghz_test_carrier.h
applications/debug/uart_echo/uart_echo.c
applications/debug/unit_tests/resources/unit_tests/Manifest_test
applications/debug/unit_tests/resources/unit_tests/js/basic.js
applications/debug/unit_tests/test_runner.c
applications/debug/unit_tests/tests/args/args_test.c
applications/debug/unit_tests/tests/lfrfid/lfrfid_protocols.c
applications/debug/unit_tests/tests/rpc/rpc_test.c
applications/debug/unit_tests/tests/subghz/subghz_test.c
applications/debug/unit_tests/unit_tests.c
applications/debug/usb_mouse/application.fam
applications/drivers/subghz/cc1101_ext/cc1101_ext.c
applications/drivers/subghz/cc1101_ext/cc1101_ext.h
applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.c
applications/examples/example_event_loop/example_event_loop_mutex.c
applications/main/application.fam
applications/main/archive/application.fam
applications/main/archive/archive.c
applications/main/archive/archive_i.h
applications/main/archive/helpers/archive_apps.c
applications/main/archive/helpers/archive_apps.h
applications/main/archive/helpers/archive_browser.c
applications/main/archive/helpers/archive_browser.h
applications/main/archive/helpers/archive_favorites.c
applications/main/archive/helpers/archive_favorites.h
applications/main/archive/helpers/archive_files.c
applications/main/archive/helpers/archive_files.h
applications/main/archive/scenes/archive_scene_browser.c
applications/main/archive/scenes/archive_scene_config.h
applications/main/archive/scenes/archive_scene_delete.c
applications/main/archive/scenes/archive_scene_rename.c
applications/main/archive/views/archive_browser_view.c
applications/main/archive/views/archive_browser_view.h
applications/main/bad_usb/application.fam
applications/main/bad_usb/bad_usb_app.c
applications/main/bad_usb/bad_usb_app_i.h
applications/main/bad_usb/helpers/bad_usb_hid.c
applications/main/bad_usb/helpers/bad_usb_hid.h
applications/main/bad_usb/helpers/ducky_script.c
applications/main/bad_usb/helpers/ducky_script.h
applications/main/bad_usb/helpers/ducky_script_commands.c
applications/main/bad_usb/helpers/ducky_script_i.h
applications/main/bad_usb/scenes/bad_usb_scene_config.c
applications/main/bad_usb/scenes/bad_usb_scene_config.h
applications/main/bad_usb/scenes/bad_usb_scene_config_layout.c
applications/main/bad_usb/scenes/bad_usb_scene_confirm_unpair.c
applications/main/bad_usb/scenes/bad_usb_scene_error.c
applications/main/bad_usb/scenes/bad_usb_scene_file_select.c
applications/main/bad_usb/scenes/bad_usb_scene_work.c
applications/main/bad_usb/views/bad_usb_view.c
applications/main/gpio/gpio_app.c
applications/main/gpio/gpio_app_i.h
applications/main/gpio/gpio_custom_event.h
applications/main/gpio/scenes/gpio_scene_config.h
applications/main/gpio/scenes/gpio_scene_start.c
applications/main/ibutton/ibutton.c
applications/main/ibutton/ibutton_i.h
applications/main/ibutton/scenes/ibutton_scene_add_type.c
applications/main/ibutton/scenes/ibutton_scene_delete_confirm.c
applications/main/ibutton/scenes/ibutton_scene_emulate.c
applications/main/ibutton/scenes/ibutton_scene_info.c
applications/main/ibutton/scenes/ibutton_scene_save_name.c
applications/main/ibutton/scenes/ibutton_scene_write.c
applications/main/infrared/infrared_app.c
applications/main/infrared/infrared_app_i.h
applications/main/infrared/infrared_cli.c
applications/main/infrared/resources/infrared/assets/ac.ir
applications/main/infrared/resources/infrared/assets/audio.ir
applications/main/infrared/resources/infrared/assets/tv.ir
applications/main/infrared/scenes/infrared_scene_config.h
applications/main/infrared/scenes/infrared_scene_learn.c
applications/main/infrared/scenes/infrared_scene_learn_enter_name.c
applications/main/infrared/scenes/infrared_scene_learn_success.c
applications/main/infrared/scenes/infrared_scene_remote_list.c
applications/main/infrared/scenes/infrared_scene_start.c
applications/main/infrared/scenes/infrared_scene_universal.c
applications/main/infrared/scenes/infrared_scene_universal_ac.c
applications/main/infrared/scenes/infrared_scene_universal_audio.c
applications/main/infrared/scenes/infrared_scene_universal_projector.c
applications/main/infrared/scenes/infrared_scene_universal_tv.c
applications/main/lfrfid/lfrfid.c
applications/main/lfrfid/lfrfid_i.h
applications/main/lfrfid/scenes/lfrfid_scene_config.h
applications/main/lfrfid/scenes/lfrfid_scene_emulate.c
applications/main/lfrfid/scenes/lfrfid_scene_extra_actions.c
applications/main/lfrfid/scenes/lfrfid_scene_raw_name.c
applications/main/lfrfid/scenes/lfrfid_scene_read_success.c
applications/main/lfrfid/scenes/lfrfid_scene_save_name.c
applications/main/lfrfid/scenes/lfrfid_scene_save_type.c
applications/main/lfrfid/scenes/lfrfid_scene_saved_key_menu.c
applications/main/lfrfid/scenes/lfrfid_scene_write.c
applications/main/nfc/api/nfc_app_api_table_i.h
applications/main/nfc/application.fam
applications/main/nfc/cli/commands/dump/nfc_cli_command_dump.c
applications/main/nfc/cli/commands/helpers/nfc_cli_format.c
applications/main/nfc/cli/commands/raw/nfc_cli_command_raw.c
applications/main/nfc/helpers/nfc_custom_event.h
applications/main/nfc/helpers/nfc_emv_parser.c
applications/main/nfc/helpers/nfc_emv_parser.h
applications/main/nfc/helpers/nfc_supported_cards.c
applications/main/nfc/helpers/nfc_supported_cards.h
applications/main/nfc/helpers/protocol_support/felica/felica.c
applications/main/nfc/helpers/protocol_support/iso14443_3a/iso14443_3a.c
applications/main/nfc/helpers/protocol_support/iso14443_3b/iso14443_3b.c
applications/main/nfc/helpers/protocol_support/iso14443_4a/iso14443_4a.c
applications/main/nfc/helpers/protocol_support/iso14443_4b/iso14443_4b.c
applications/main/nfc/helpers/protocol_support/iso15693_3/iso15693_3.c
applications/main/nfc/helpers/protocol_support/mf_classic/mf_classic.c
applications/main/nfc/helpers/protocol_support/mf_desfire/mf_desfire.c
applications/main/nfc/helpers/protocol_support/mf_plus/mf_plus.c
applications/main/nfc/helpers/protocol_support/mf_plus/mf_plus_render.c
applications/main/nfc/helpers/protocol_support/mf_ultralight/mf_ultralight.c
applications/main/nfc/helpers/protocol_support/mf_ultralight/mf_ultralight_render.c
applications/main/nfc/helpers/protocol_support/nfc_protocol_support.c
applications/main/nfc/helpers/protocol_support/nfc_protocol_support.h
applications/main/nfc/helpers/protocol_support/nfc_protocol_support_base.h
applications/main/nfc/helpers/protocol_support/nfc_protocol_support_common.h
applications/main/nfc/helpers/protocol_support/nfc_protocol_support_gui_common.c
applications/main/nfc/helpers/protocol_support/nfc_protocol_support_gui_common.h
applications/main/nfc/helpers/protocol_support/nfc_protocol_support_unlock_helper.h
applications/main/nfc/helpers/protocol_support/slix/slix.c
applications/main/nfc/helpers/protocol_support/st25tb/st25tb.c
applications/main/nfc/nfc_app.c
applications/main/nfc/nfc_app_i.h
applications/main/nfc/plugins/supported_cards/aime.c
applications/main/nfc/plugins/supported_cards/all_in_one.c
applications/main/nfc/plugins/supported_cards/bip.c
applications/main/nfc/plugins/supported_cards/clipper.c
applications/main/nfc/plugins/supported_cards/gallagher.c
applications/main/nfc/plugins/supported_cards/hi.c
applications/main/nfc/plugins/supported_cards/hid.c
applications/main/nfc/plugins/supported_cards/itso.c
applications/main/nfc/plugins/supported_cards/microel.c
applications/main/nfc/plugins/supported_cards/mizip.c
applications/main/nfc/plugins/supported_cards/mykey.c
applications/main/nfc/plugins/supported_cards/myki.c
applications/main/nfc/plugins/supported_cards/ndef.c
applications/main/nfc/plugins/supported_cards/opal.c
applications/main/nfc/plugins/supported_cards/plantain.c
applications/main/nfc/plugins/supported_cards/two_cities.c
applications/main/nfc/plugins/supported_cards/umarsh.c
applications/main/nfc/plugins/supported_cards/washcity.c
applications/main/nfc/resources/nfc/assets/mf_classic_dict.nfc
applications/main/nfc/resources/nfc/assets/mf_ultralight_c_dict.nfc
applications/main/nfc/scenes/nfc_scene_config.h
applications/main/nfc/scenes/nfc_scene_emulate.c
applications/main/nfc/scenes/nfc_scene_mf_classic_dict_attack.c
applications/main/nfc/scenes/nfc_scene_mf_classic_update_initial_wrong_card.c
applications/main/nfc/scenes/nfc_scene_mf_ultralight_c_dict_attack.c
applications/main/nfc/scenes/nfc_scene_set_atqa.c
applications/main/nfc/scenes/nfc_scene_set_type.c
applications/main/nfc/scenes/nfc_scene_slix_unlock.c
applications/main/nfc/scenes/nfc_scene_start.c
applications/main/subghz/application.fam
applications/main/subghz/helpers/subghz_chat.c
applications/main/subghz/helpers/subghz_custom_event.h
applications/main/subghz/helpers/subghz_frequency_analyzer_worker.c
applications/main/subghz/helpers/subghz_frequency_analyzer_worker.h
applications/main/subghz/helpers/subghz_txrx.c
applications/main/subghz/helpers/subghz_txrx.h
applications/main/subghz/helpers/subghz_txrx_create_protocol_key.c
applications/main/subghz/helpers/subghz_txrx_create_protocol_key.h
applications/main/subghz/helpers/subghz_txrx_i.h
applications/main/subghz/helpers/subghz_types.h
applications/main/subghz/resources/subghz/assets/keeloq_mfcodes
applications/main/subghz/resources/subghz/assets/keeloq_mfcodes_user.example
applications/main/subghz/resources/subghz/assets/setting_user.example
applications/main/subghz/scenes/subghz_scene_config.h
applications/main/subghz/scenes/subghz_scene_delete.c
applications/main/subghz/scenes/subghz_scene_delete_raw.c
applications/main/subghz/scenes/subghz_scene_frequency_analyzer.c
applications/main/subghz/scenes/subghz_scene_more_raw.c
applications/main/subghz/scenes/subghz_scene_need_saving.c
applications/main/subghz/scenes/subghz_scene_read_raw.c
applications/main/subghz/scenes/subghz_scene_receiver.c
applications/main/subghz/scenes/subghz_scene_receiver_config.c
applications/main/subghz/scenes/subghz_scene_receiver_info.c
applications/main/subghz/scenes/subghz_scene_rpc.c
applications/main/subghz/scenes/subghz_scene_save_name.c
applications/main/subghz/scenes/subghz_scene_save_success.c
applications/main/subghz/scenes/subghz_scene_saved.c
applications/main/subghz/scenes/subghz_scene_saved_menu.c
applications/main/subghz/scenes/subghz_scene_set_type.c
applications/main/subghz/scenes/subghz_scene_show_error.c
applications/main/subghz/scenes/subghz_scene_start.c
applications/main/subghz/scenes/subghz_scene_transmitter.c
applications/main/subghz/subghz.c
applications/main/subghz/subghz_cli.c
applications/main/subghz/subghz_history.c
applications/main/subghz/subghz_history.h
applications/main/subghz/subghz_i.c
applications/main/subghz/subghz_i.h
applications/main/subghz/views/receiver.c
applications/main/subghz/views/receiver.h
applications/main/subghz/views/subghz_frequency_analyzer.c
applications/main/subghz/views/subghz_frequency_analyzer.h
applications/main/subghz/views/subghz_read_raw.c
applications/main/subghz/views/subghz_read_raw.h
applications/main/subghz/views/transmitter.c
applications/main/subghz/views/transmitter.h
applications/main/u2f/scenes/u2f_scene_error.c
applications/main/u2f/u2f_app.c
applications/main/u2f/u2f_app_i.h
applications/main/u2f/u2f_data.c
applications/main/u2f/u2f_data.h
applications/main/u2f/views/u2f_view.c
applications/services/application.fam
applications/services/applications.h
applications/services/bt/application.fam
applications/services/bt/bt_cli.c
applications/services/bt/bt_service/bt.c
applications/services/bt/bt_service/bt_api.c
applications/services/bt/bt_service/bt_i.h
applications/services/bt/bt_service/bt_keys_filename.h
applications/services/bt/bt_settings.c
applications/services/bt/bt_settings_filename.h
applications/services/cli/application.fam
applications/services/cli/cli_main_commands.c
applications/services/cli/cli_main_shell.c
applications/services/cli/cli_vcp.c
applications/services/crypto/application.fam
applications/services/crypto/crypto_cli.c
applications/services/desktop/animations/animation_manager.c
applications/services/desktop/animations/animation_manager.h
applications/services/desktop/animations/animation_storage.c
applications/services/desktop/animations/animation_storage.h
applications/services/desktop/animations/views/bubble_animation_view.c
applications/services/desktop/animations/views/one_shot_animation_view.c
applications/services/desktop/desktop.c
applications/services/desktop/desktop_i.h
applications/services/desktop/desktop_settings.c
applications/services/desktop/desktop_settings.h
applications/services/desktop/desktop_settings_filename.h
applications/services/desktop/helpers/pin_code.c
applications/services/desktop/helpers/pin_code.h
applications/services/desktop/helpers/slideshow.c
applications/services/desktop/helpers/slideshow.h
applications/services/desktop/scenes/desktop_scene_config.h
applications/services/desktop/scenes/desktop_scene_lock_menu.c
applications/services/desktop/scenes/desktop_scene_locked.c
applications/services/desktop/scenes/desktop_scene_main.c
applications/services/desktop/scenes/desktop_scene_pin_input.c
applications/services/desktop/scenes/desktop_scene_slideshow.c
applications/services/desktop/views/desktop_events.h
applications/services/desktop/views/desktop_view_debug.h
applications/services/desktop/views/desktop_view_lock_menu.c
applications/services/desktop/views/desktop_view_lock_menu.h
applications/services/desktop/views/desktop_view_locked.c
applications/services/desktop/views/desktop_view_locked.h
applications/services/desktop/views/desktop_view_main.c
applications/services/desktop/views/desktop_view_main.h
applications/services/desktop/views/desktop_view_slideshow.c
applications/services/dialogs/dialogs.c
applications/services/dialogs/dialogs.h
applications/services/dialogs/dialogs_api.c
applications/services/dialogs/dialogs_message.h
applications/services/dialogs/dialogs_module_file_browser.c
applications/services/dolphin/dolphin.c
applications/services/dolphin/dolphin.h
applications/services/dolphin/helpers/dolphin_deed.c
applications/services/dolphin/helpers/dolphin_deed.h
applications/services/dolphin/helpers/dolphin_state.c
applications/services/dolphin/helpers/dolphin_state.h
applications/services/dolphin/helpers/dolphin_state_filename.h
applications/services/expansion/expansion.c
applications/services/expansion/expansion.h
applications/services/expansion/expansion_settings.c
applications/services/expansion/expansion_settings_filename.h
applications/services/expansion/expansion_worker.c
applications/services/expansion/expansion_worker.h
applications/services/gui/canvas.c
applications/services/gui/canvas.h
applications/services/gui/elements.c
applications/services/gui/elements.h
applications/services/gui/gui.c
applications/services/gui/gui.h
applications/services/gui/gui_i.h
applications/services/gui/icon_animation.c
applications/services/gui/modules/button_panel.h
applications/services/gui/modules/byte_input.c
applications/services/gui/modules/date_time_input.c
applications/services/gui/modules/file_browser.c
applications/services/gui/modules/file_browser.h
applications/services/gui/modules/file_browser_worker.c
applications/services/gui/modules/file_browser_worker.h
applications/services/gui/modules/menu.c
applications/services/gui/modules/menu.h
applications/services/gui/modules/number_input.c
applications/services/gui/modules/submenu.c
applications/services/gui/modules/submenu.h
applications/services/gui/modules/text_input.c
applications/services/gui/modules/text_input.h
applications/services/gui/modules/variable_item_list.c
applications/services/gui/modules/variable_item_list.h
applications/services/gui/modules/widget.c
applications/services/gui/modules/widget.h
applications/services/gui/modules/widget_elements/widget_element_i.h
applications/services/gui/modules/widget_elements/widget_element_text_box.c
applications/services/gui/scene_manager.c
applications/services/gui/view.c
applications/services/gui/view.h
applications/services/gui/view_dispatcher.c
applications/services/gui/view_dispatcher.h
applications/services/gui/view_dispatcher_i.h
applications/services/gui/view_holder.c
applications/services/gui/view_i.h
applications/services/gui/view_port.c
applications/services/gui/view_port.h
applications/services/gui/view_port_i.h
applications/services/gui/view_stack.c
applications/services/gui/view_stack.h
applications/services/input/application.fam
applications/services/input/input.c
applications/services/input/input.h
applications/services/input/input_cli.c
applications/services/loader/application.fam
applications/services/loader/loader.c
applications/services/loader/loader.h
applications/services/loader/loader_applications.c
applications/services/loader/loader_cli.c
applications/services/loader/loader_i.h
applications/services/loader/loader_menu.c
applications/services/loader/loader_menu.h
applications/services/notification/notification_app.c
applications/services/notification/notification_app.h
applications/services/notification/notification_settings_filename.h
applications/services/power/application.fam
applications/services/power/power_cli.c
applications/services/power/power_service/power.c
applications/services/power/power_service/power_api.c
applications/services/power/power_service/power_i.h
applications/services/rpc/rpc.c
applications/services/rpc/rpc.h
applications/services/rpc/rpc_gui.c
applications/services/rpc/rpc_storage.c
applications/services/storage/application.fam
applications/services/storage/filesystem_api_internal.h
applications/services/storage/storage.c
applications/services/storage/storage.h
applications/services/storage/storage_cli.c
applications/services/storage/storage_external_api.c
applications/services/storage/storage_glue.c
applications/services/storage/storage_glue.h
applications/services/storage/storage_i.h
applications/services/storage/storage_message.h
applications/services/storage/storage_processing.c
applications/services/storage/storage_processing.h
applications/services/storage/storages/storage_ext.c
applications/services/storage/storages/storage_ext.h
applications/settings/about/about.c
applications/settings/about/application.fam
applications/settings/application.fam
applications/settings/bt_settings_app/application.fam
applications/settings/bt_settings_app/scenes/bt_settings_scene_forget_dev_confirm.c
applications/settings/clock_settings/application.fam
applications/settings/clock_settings/clock_settings_alarm.c
applications/settings/desktop_settings/application.fam
applications/settings/desktop_settings/desktop_settings_app.c
applications/settings/desktop_settings/desktop_settings_app.h
applications/settings/desktop_settings/desktop_settings_custom_event.h
applications/settings/desktop_settings/scenes/desktop_settings_scene_config.h
applications/settings/desktop_settings/scenes/desktop_settings_scene_i.h
applications/settings/desktop_settings/scenes/desktop_settings_scene_start.c
applications/settings/desktop_settings/views/desktop_settings_view_pin_setup_howto2.c
applications/settings/dolphin_passport/application.fam
applications/settings/dolphin_passport/passport.c
applications/settings/expansion_settings_app/application.fam
applications/settings/notification_settings/application.fam
applications/settings/notification_settings/notification_settings_app.c
applications/settings/power_settings_app/application.fam
applications/settings/power_settings_app/power_settings_app.c
applications/settings/power_settings_app/power_settings_app.h
applications/settings/power_settings_app/scenes/power_settings_scene_battery_info.c
applications/settings/power_settings_app/scenes/power_settings_scene_power_off.c
applications/settings/power_settings_app/scenes/power_settings_scene_start.c
applications/settings/power_settings_app/views/battery_info.c
applications/settings/power_settings_app/views/battery_info.h
applications/settings/storage_settings/application.fam
applications/settings/storage_settings/scenes/storage_settings_scene_benchmark.c
applications/settings/storage_settings/scenes/storage_settings_scene_benchmark_confirm.c
applications/settings/storage_settings/scenes/storage_settings_scene_config.h
applications/settings/storage_settings/scenes/storage_settings_scene_factory_reset.c
applications/settings/storage_settings/scenes/storage_settings_scene_format_confirm.c
applications/settings/storage_settings/scenes/storage_settings_scene_formatting.c
applications/settings/storage_settings/scenes/storage_settings_scene_internal_info.c
applications/settings/storage_settings/scenes/storage_settings_scene_sd_info.c
applications/settings/storage_settings/scenes/storage_settings_scene_unmount_confirm.c
applications/settings/storage_settings/scenes/storage_settings_scene_unmounted.c
applications/settings/storage_settings/storage_settings.c
applications/settings/storage_settings/storage_settings.h
applications/settings/system/application.fam
applications/settings/system/system_settings.c
applications/system/application.fam
applications/system/hid_app/application.fam
applications/system/hid_app/hid.c
applications/system/hid_app/hid.h
applications/system/hid_app/scenes/hid_scene_config.h
applications/system/hid_app/scenes/hid_scene_start.c
applications/system/hid_app/scenes/hid_scene_unpair.c
applications/system/hid_app/views.h
applications/system/hid_app/views/hid_media.c
applications/system/hid_app/views/hid_mouse.c
applications/system/hid_app/views/hid_mouse_jiggler.c
applications/system/hid_app/views/hid_mouse_jiggler.h
applications/system/hid_app/views/hid_tiktok.c
applications/system/js_app/application.fam
applications/system/js_app/examples/apps/Scripts/interactive.js
applications/system/js_app/js_app.c
applications/system/js_app/js_modules.c
applications/system/js_app/js_modules.h
applications/system/js_app/modules/js_badusb.c
applications/system/js_app/modules/js_event_loop/js_event_loop.c
applications/system/js_app/modules/js_flipper.c
applications/system/js_app/modules/js_gui/icon.c
applications/system/js_app/modules/js_gui/text_input.c
applications/system/js_app/modules/js_storage.c
applications/system/js_app/packages/create-fz-app/README.md
applications/system/js_app/packages/create-fz-app/package.json
applications/system/js_app/packages/create-fz-app/template/index.ts
applications/system/js_app/packages/create-fz-app/template/package.json
applications/system/js_app/packages/create-fz-app/template/tsconfig.json
applications/system/js_app/packages/fz-sdk/README.md
applications/system/js_app/packages/fz-sdk/badusb/index.d.ts
applications/system/js_app/packages/fz-sdk/flipper/index.d.ts
applications/system/js_app/packages/fz-sdk/gpio/index.d.ts
applications/system/js_app/packages/fz-sdk/gui/icon.d.ts
applications/system/js_app/packages/fz-sdk/gui/text_input.d.ts
applications/system/js_app/packages/fz-sdk/package.json
applications/system/js_app/packages/fz-sdk/sdk.js
applications/system/js_app/packages/fz-sdk/storage/index.d.ts
applications/system/updater/util/update_task.c
applications/system/updater/util/update_task_worker_backup.c
applications/system/updater/util/update_task_worker_flasher.c
applications/system/updater/views/updater_main.c
assets/ReadMe.md
assets/SConscript
assets/dolphin/ReadMe.md
assets/dolphin/blocking/L0_NoDb_128x51/frame_0.png
assets/dolphin/blocking/L0_NoDb_128x51/meta.txt
assets/dolphin/blocking/L0_SdBad_128x51/frame_0.png
assets/dolphin/blocking/L0_SdBad_128x51/meta.txt
assets/dolphin/blocking/L0_SdOk_128x51/frame_0.png
assets/dolphin/blocking/L0_SdOk_128x51/meta.txt
assets/dolphin/blocking/L0_Url_128x51/frame_0.png
assets/dolphin/blocking/L0_Url_128x51/meta.txt
assets/dolphin/external/L1_Akira_128x64/meta.txt
assets/dolphin/external/L1_Boxing_128x64/meta.txt
assets/dolphin/external/L1_Cry_128x64/meta.txt
assets/dolphin/external/L1_Doom_128x64/meta.txt
assets/dolphin/external/L1_Furippa1_128x64/meta.txt
assets/dolphin/external/L1_Kaiju_128x64/meta.txt
assets/dolphin/external/L1_Laptop_128x51/meta.txt
assets/dolphin/external/L1_Leaving_sad_128x64/meta.txt
assets/dolphin/external/L1_Mad_fist_128x64/meta.txt
assets/dolphin/external/L1_Mods_128x64/meta.txt
assets/dolphin/external/L1_My_dude_128x64/meta.txt
assets/dolphin/external/L1_Painting_128x64/meta.txt
assets/dolphin/external/L1_Procrastinating_128x64/meta.txt
assets/dolphin/external/L1_Read_books_128x64/meta.txt
assets/dolphin/external/L1_Recording_128x51/meta.txt
assets/dolphin/external/L1_Sad_song_128x64/meta.txt
assets/dolphin/external/L1_Senpai_128x64/meta.txt
assets/dolphin/external/L1_Showtime_128x64/meta.txt
assets/dolphin/external/L1_Sleep_128x64/meta.txt
assets/dolphin/external/L1_Waves_128x50/meta.txt
assets/dolphin/external/L2_Coding_in_the_shell_128x64/meta.txt
assets/dolphin/external/L2_Dj_128x64/meta.txt
assets/dolphin/external/L2_Furippa2_128x64/meta.txt
assets/dolphin/external/L2_Hacking_pc_128x64/meta.txt
assets/dolphin/external/L2_Secret_door_128x64/meta.txt
assets/dolphin/external/L2_Soldering_128x64/meta.txt
assets/dolphin/external/L2_Wake_up_128x64/meta.txt
assets/dolphin/external/L3_Freedom_2_dolphins_128x64/meta.txt
assets/dolphin/external/L3_Furippa3_128x64/meta.txt
assets/dolphin/external/L3_Hijack_radio_128x64/meta.txt
assets/dolphin/external/L3_Intruder_alert_128x64/meta.txt
assets/dolphin/external/L3_Lab_research_128x54/meta.txt
assets/dolphin/external/manifest.txt
assets/dolphin/internal/L1_BadBattery_128x47/frame_0.png
assets/dolphin/internal/L1_BadBattery_128x47/meta.txt
assets/dolphin/internal/L1_NoSd_128x49/frame_0.png
assets/dolphin/internal/L1_NoSd_128x49/meta.txt
assets/dolphin/internal/manifest.txt
assets/icons/Common/DFU_128x50.png
assets/icons/ErasePin/Erase_pin_128x64.png
assets/icons/Update/Updating_32x40.png
assets/slideshow/update_default/frame_00.png
documentation/devboard/Get started with the Dev Board.md
documentation/doxygen/Doxyfile.cfg
documentation/doxygen/examples.dox
documentation/doxygen/index.dox
documentation/file_formats/BadUsbScriptFormat.md
documentation/file_formats/iButtonFileFormat.md
documentation/images/byte_input.png
documentation/js/js_badusb.md
documentation/js/js_builtin.md
documentation/js/js_developing_apps_using_js_sdk.md
documentation/js/js_gui.md
documentation/js/js_gui__icon.md
documentation/js/js_gui__text_input.md
documentation/js/js_notification.md
documentation/js/js_serial.md
documentation/js/js_storage.md
documentation/js/js_your_first_js_app.md
fbt_options.py
firmware.scons
furi/core/check.c
furi/core/check.h
furi/core/event_loop.c
furi/core/event_loop.h
furi/core/thread.c
furi/core/timer.c
furi/core/timer.h
furi/flipper.c
furi/flipper.h
lib/SConscript
lib/bit_lib/bit_lib.c
lib/ble_profile/extra_profiles/hid_profile.c
lib/drivers/SConscript
lib/fatfs/ff.c
lib/flipper_application/application_manifest.h
lib/flipper_application/flipper_application.c
lib/ibutton/protocols/dallas/protocol_group_dallas_defs.c
lib/ibutton/protocols/dallas/protocol_group_dallas_defs.h
lib/infrared/encoder_decoder/common/infrared_common_decoder.c
lib/infrared/signal/infrared_brute_force.c
lib/infrared/signal/infrared_brute_force.h
lib/infrared/signal/infrared_signal.c
lib/infrared/worker/infrared_worker.c
lib/infrared/worker/infrared_worker.h
lib/lfrfid/lfrfid_worker.c
lib/lfrfid/lfrfid_worker.h
lib/lfrfid/lfrfid_worker_i.h
lib/lfrfid/lfrfid_worker_modes.c
lib/lfrfid/protocols/lfrfid_protocols.c
lib/lfrfid/protocols/lfrfid_protocols.h
lib/lfrfid/protocols/protocol_electra.c
lib/lfrfid/protocols/protocol_em4100.c
lib/lfrfid/protocols/protocol_fdx_b.c
lib/lfrfid/protocols/protocol_gproxii.c
lib/lfrfid/protocols/protocol_jablotron.c
lib/lfrfid/protocols/protocol_pac_stanley.c
lib/lfrfid/protocols/protocol_pyramid.c
lib/lfrfid/protocols/protocol_securakey.c
lib/mjs/mjs_exec.c
lib/music_worker/music_worker.c
lib/nfc/SConscript
lib/nfc/helpers/iso14443_4_layer.c
lib/nfc/helpers/iso14443_4_layer.h
lib/nfc/protocols/felica/felica_poller.c
lib/nfc/protocols/iso14443_3a/iso14443_3a_listener.h
lib/nfc/protocols/iso14443_3a/iso14443_3a_listener_i.h
lib/nfc/protocols/iso14443_4a/iso14443_4a.h
lib/nfc/protocols/iso14443_4a/iso14443_4a_i.h
lib/nfc/protocols/iso14443_4a/iso14443_4a_listener.c
lib/nfc/protocols/iso14443_4a/iso14443_4a_listener.h
lib/nfc/protocols/iso14443_4a/iso14443_4a_listener_i.c
lib/nfc/protocols/iso14443_4a/iso14443_4a_listener_i.h
lib/nfc/protocols/iso14443_4a/iso14443_4a_poller.h
lib/nfc/protocols/iso14443_4a/iso14443_4a_poller_i.c
lib/nfc/protocols/iso14443_4b/iso14443_4b_poller_i.c
lib/nfc/protocols/iso15693_3/iso15693_3.c
lib/nfc/protocols/iso15693_3/iso15693_3.h
lib/nfc/protocols/iso15693_3/iso15693_3_i.c
lib/nfc/protocols/iso15693_3/iso15693_3_i.h
lib/nfc/protocols/iso15693_3/iso15693_3_listener_i.h
lib/nfc/protocols/iso15693_3/iso15693_3_poller.h
lib/nfc/protocols/iso15693_3/iso15693_3_poller_i.c
lib/nfc/protocols/iso15693_3/iso15693_3_poller_i.h
lib/nfc/protocols/mf_desfire/mf_desfire.c
lib/nfc/protocols/mf_desfire/mf_desfire.h
lib/nfc/protocols/mf_desfire/mf_desfire_i.c
lib/nfc/protocols/mf_desfire/mf_desfire_i.h
lib/nfc/protocols/mf_desfire/mf_desfire_poller.c
lib/nfc/protocols/mf_desfire/mf_desfire_poller.h
lib/nfc/protocols/mf_desfire/mf_desfire_poller_i.c
lib/nfc/protocols/mf_desfire/mf_desfire_poller_i.h
lib/nfc/protocols/mf_plus/mf_plus_i.c
lib/nfc/protocols/mf_plus/mf_plus_i.h
lib/nfc/protocols/mf_plus/mf_plus_poller_i.c
lib/nfc/protocols/mf_ultralight/mf_ultralight.c
lib/nfc/protocols/mf_ultralight/mf_ultralight_listener.c
lib/nfc/protocols/mf_ultralight/mf_ultralight_poller.c
lib/nfc/protocols/mf_ultralight/mf_ultralight_poller.h
lib/nfc/protocols/mf_ultralight/mf_ultralight_poller_i.h
lib/nfc/protocols/nfc_device_defs.c
lib/nfc/protocols/nfc_listener_defs.c
lib/nfc/protocols/nfc_poller_defs.c
lib/nfc/protocols/nfc_protocol.c
lib/nfc/protocols/nfc_protocol.h
lib/nfc/protocols/slix/slix_poller.c
lib/nfc/protocols/slix/slix_poller.h
lib/nfc/protocols/slix/slix_poller_i.c
lib/signal_reader/parsers/iso15693/iso15693_parser.c
lib/subghz/SConscript
lib/subghz/blocks/decoder.c
lib/subghz/blocks/decoder.h
lib/subghz/blocks/generic.c
lib/subghz/blocks/generic.h
lib/subghz/devices/cc1101_configs.c
lib/subghz/devices/cc1101_configs.h
lib/subghz/devices/cc1101_int/cc1101_int_interconnect.c
lib/subghz/devices/devices.c
lib/subghz/devices/devices.h
lib/subghz/devices/preset.h
lib/subghz/devices/types.h
lib/subghz/environment.c
lib/subghz/environment.h
lib/subghz/protocols/alutech_at_4n.c
lib/subghz/protocols/alutech_at_4n.h
lib/subghz/protocols/ansonic.c
lib/subghz/protocols/ansonic.h
lib/subghz/protocols/base.c
lib/subghz/protocols/base.h
lib/subghz/protocols/bett.c
lib/subghz/protocols/bett.h
lib/subghz/protocols/bin_raw.c
lib/subghz/protocols/bin_raw.h
lib/subghz/protocols/came.c
lib/subghz/protocols/came.h
lib/subghz/protocols/came_atomo.c
lib/subghz/protocols/came_atomo.h
lib/subghz/protocols/came_twee.c
lib/subghz/protocols/came_twee.h
lib/subghz/protocols/chamberlain_code.c
lib/subghz/protocols/chamberlain_code.h
lib/subghz/protocols/clemsa.c
lib/subghz/protocols/clemsa.h
lib/subghz/protocols/dickert_mahs.c
lib/subghz/protocols/dickert_mahs.h
lib/subghz/protocols/doitrand.c
lib/subghz/protocols/doitrand.h
lib/subghz/protocols/dooya.c
lib/subghz/protocols/dooya.h
lib/subghz/protocols/elplast.c
lib/subghz/protocols/elplast.h
lib/subghz/protocols/faac_slh.c
lib/subghz/protocols/faac_slh.h
lib/subghz/protocols/feron.c
lib/subghz/protocols/feron.h
lib/subghz/protocols/gangqi.c
lib/subghz/protocols/gangqi.h
lib/subghz/protocols/gate_tx.c
lib/subghz/protocols/gate_tx.h
lib/subghz/protocols/hay21.c
lib/subghz/protocols/hay21.h
lib/subghz/protocols/hollarm.c
lib/subghz/protocols/hollarm.h
lib/subghz/protocols/holtek.c
lib/subghz/protocols/holtek.h
lib/subghz/protocols/holtek_ht12x.c
lib/subghz/protocols/holtek_ht12x.h
lib/subghz/protocols/honeywell_wdb.c
lib/subghz/protocols/honeywell_wdb.h
lib/subghz/protocols/hormann.c
lib/subghz/protocols/hormann.h
lib/subghz/protocols/ido.c
lib/subghz/protocols/ido.h
lib/subghz/protocols/intertechno_v3.c
lib/subghz/protocols/intertechno_v3.h
lib/subghz/protocols/keeloq.c
lib/subghz/protocols/keeloq.h
lib/subghz/protocols/keeloq_common.c
lib/subghz/protocols/keeloq_common.h
lib/subghz/protocols/kinggates_stylo_4k.c
lib/subghz/protocols/kinggates_stylo_4k.h
lib/subghz/protocols/legrand.c
lib/subghz/protocols/legrand.h
lib/subghz/protocols/linear.c
lib/subghz/protocols/linear.h
lib/subghz/protocols/linear_delta3.c
lib/subghz/protocols/linear_delta3.h
lib/subghz/protocols/magellan.c
lib/subghz/protocols/magellan.h
lib/subghz/protocols/marantec.c
lib/subghz/protocols/marantec.h
lib/subghz/protocols/marantec24.c
lib/subghz/protocols/marantec24.h
lib/subghz/protocols/mastercode.c
lib/subghz/protocols/mastercode.h
lib/subghz/protocols/megacode.c
lib/subghz/protocols/megacode.h
lib/subghz/protocols/nero_radio.c
lib/subghz/protocols/nero_radio.h
lib/subghz/protocols/nero_sketch.c
lib/subghz/protocols/nero_sketch.h
lib/subghz/protocols/nice_flo.c
lib/subghz/protocols/nice_flo.h
lib/subghz/protocols/nice_flor_s.c
lib/subghz/protocols/nice_flor_s.h
lib/subghz/protocols/phoenix_v2.c
lib/subghz/protocols/phoenix_v2.h
lib/subghz/protocols/power_smart.c
lib/subghz/protocols/power_smart.h
lib/subghz/protocols/princeton.c
lib/subghz/protocols/princeton.h
lib/subghz/protocols/protocol_items.c
lib/subghz/protocols/protocol_items.h
lib/subghz/protocols/public_api.h
lib/subghz/protocols/raw.c
lib/subghz/protocols/revers_rb2.c
lib/subghz/protocols/roger.c
lib/subghz/protocols/secplus_v1.c
lib/subghz/protocols/secplus_v1.h
lib/subghz/protocols/secplus_v2.c
lib/subghz/protocols/secplus_v2.h
lib/subghz/protocols/smc5326.c
lib/subghz/protocols/smc5326.h
lib/subghz/protocols/somfy_keytis.c
lib/subghz/protocols/somfy_keytis.h
lib/subghz/protocols/somfy_telis.c
lib/subghz/protocols/somfy_telis.h
lib/subghz/receiver.c
lib/subghz/receiver.h
lib/subghz/subghz_file_encoder_worker.c
lib/subghz/subghz_file_encoder_worker.h
lib/subghz/subghz_keystore.c
lib/subghz/subghz_keystore.h
lib/subghz/subghz_setting.c
lib/subghz/subghz_setting.h
lib/subghz/subghz_tx_rx_worker.c
lib/subghz/types.h
lib/toolbox/args.c
lib/toolbox/cli/cli_command.h
lib/toolbox/cli/shell/cli_shell.c
lib/toolbox/compress.c
lib/toolbox/compress.h
lib/toolbox/dir_walk.c
lib/toolbox/dir_walk.h
lib/toolbox/name_generator.c
lib/toolbox/name_generator.h
lib/toolbox/path.c
lib/toolbox/path.h
lib/toolbox/tar/tar_archive.c
lib/toolbox/tar/tar_archive.h
lib/toolbox/version.c
lib/toolbox/version.h
lib/u8g2/u8g2.h
lib/u8g2/u8g2_buffer.c
lib/u8g2/u8g2_fonts.c
lib/update_util/update_operation.h
scripts/assets.py
scripts/debug/FreeRTOS/FreeRTOSgdb/QueueTools.py
scripts/fbt/appmanifest.py
scripts/fbt/elfmanifest.py
scripts/fbt/sdk/cache.py
scripts/fbt_tools/fbt_apps.py
scripts/fbt_tools/fbt_assets.py
scripts/fbt_tools/fbt_dist.py
scripts/fbt_tools/fbt_extapps.py
scripts/fbt_tools/fbt_help.py
scripts/fbt_tools/fbt_hwtarget.py
scripts/fbt_tools/fbt_resources.py
scripts/flipper/assets/tarball.py
scripts/flipper/storage.py
scripts/get_env.py
scripts/imglint.py
scripts/selfupdate.py
scripts/toolchain/fbtenv.sh
scripts/ufbt/SConstruct
scripts/update.py
scripts/version.py
site_scons/commandline.scons
site_scons/firmwareopts.scons
targets/f18/api_symbols.csv
targets/f7/api_symbols.csv
targets/f7/ble_glue/gap.h
targets/f7/fatfs/fatfs.h
targets/f7/fatfs/ffconf.h
targets/f7/furi_hal/furi_hal.c
targets/f7/furi_hal/furi_hal_bt.c
targets/f7/furi_hal/furi_hal_interrupt.c
targets/f7/furi_hal/furi_hal_light.c
targets/f7/furi_hal/furi_hal_region.c
targets/f7/furi_hal/furi_hal_rtc.h
targets/f7/furi_hal/furi_hal_spi_config.c
targets/f7/furi_hal/furi_hal_spi_config.h
targets/f7/furi_hal/furi_hal_subghz.c
targets/f7/furi_hal/furi_hal_subghz.h
targets/f7/furi_hal/furi_hal_usb_hid.c
targets/f7/furi_hal/furi_hal_version.c
targets/f7/src/main.c
targets/f7/target.json
targets/furi_hal_include/furi_hal.h
targets/furi_hal_include/furi_hal_usb.h
targets/furi_hal_include/furi_hal_usb_hid.h
targets/furi_hal_include/furi_hal_version.h
```

</details>

Two of the 805 content-changed files also change tracked mode:

| Path | OFW mode | MNTM mode |
|---|---:|---:|
| `applications/main/infrared/resources/infrared/assets/tv.ir` | `100644` | `100755` |
| `assets/dolphin/external/L1_Akira_128x64/meta.txt` | `100755` | `100644` |

### Shared paths with identical content but different tracked mode — 132

These 132 paths resolve to the same Git blob in both repositories but have different executable-bit modes. They are separated from content changes because a byte comparison alone would call them identical, while a checkout or packaging process can still observe their mode difference.

<details>
<summary>Show all 132 byte-identical paths with different modes</summary>

```text
100644 -> 100755  applications/system/hid_app/assets/Alt_active_17x9.png
100644 -> 100755  applications/system/hid_app/assets/Cmd_active_17x9.png
100644 -> 100755  applications/system/hid_app/assets/Ctrl_active_17x9.png
100644 -> 100755  applications/system/hid_app/assets/Enter_11x7.png
100644 -> 100755  applications/system/hid_app/assets/Tab_19x12.png
100644 -> 100755  applications/system/hid_app/assets/backslash_button_9x11.png
100644 -> 100755  applications/system/hid_app/assets/backspace_19x11.png
100644 -> 100755  applications/system/hid_app/assets/backspace_hovered_9x11.png
100644 -> 100755  applications/system/hid_app/assets/backtick_button_9x11.png
100644 -> 100755  applications/system/hid_app/assets/brace_left_button_9x11.png
100644 -> 100755  applications/system/hid_app/assets/brace_right_button_9x11.png
100644 -> 100755  applications/system/hid_app/assets/equals_button_9x11.png
100644 -> 100755  applications/system/hid_app/assets/hash_button_9x11.png
100644 -> 100755  applications/system/hid_app/assets/percent_button_9x11.png
100644 -> 100755  applications/system/hid_app/assets/quote_button_9x11.png
100644 -> 100755  applications/system/hid_app/assets/slash_button_9x11.png
100644 -> 100755  applications/system/hid_app/assets/sq_bracket_left_button_9x11.png
100644 -> 100755  applications/system/hid_app/assets/sq_bracket_right_button_9x11.png
100644 -> 100755  applications/system/hid_app/assets/underscore_button_9x11.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_0.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_1.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_10.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_11.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_12.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_13.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_14.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_15.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_16.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_17.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_18.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_19.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_2.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_20.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_21.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_22.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_23.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_24.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_25.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_26.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_27.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_28.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_29.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_3.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_30.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_31.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_32.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_33.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_34.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_35.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_36.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_37.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_38.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_39.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_4.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_40.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_41.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_42.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_43.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_44.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_45.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_46.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_47.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_48.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_49.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_5.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_50.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_51.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_52.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_53.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_54.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_55.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_56.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_6.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_7.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_8.png
100644 -> 100755  assets/dolphin/external/L3_Freedom_2_dolphins_128x64/frame_9.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_0.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_1.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_10.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_11.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_12.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_13.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_14.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_15.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_16.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_17.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_18.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_19.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_2.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_20.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_21.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_22.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_23.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_24.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_25.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_26.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_27.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_28.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_29.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_3.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_30.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_31.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_32.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_33.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_34.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_35.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_36.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_37.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_38.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_39.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_4.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_40.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_41.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_42.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_43.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_44.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_45.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_46.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_47.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_48.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_49.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_5.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_50.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_51.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_52.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_53.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_54.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_55.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_6.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_7.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_8.png
100644 -> 100755  assets/dolphin/external/L3_Intruder_alert_128x64/frame_9.png
```

</details>

### Reconciliation

The exhaustive lists reconcile to the root-tree totals:

- Unique paths: 214 OFW-only plus 1,319 MNTM-only.
- Common paths: 4,057 blobs plus 12 gitlinks, totaling 4,069.
- Common blobs: 3,120 identical in both bytes and mode, 132 identical in bytes but different in mode, and 805 with different bytes, totaling 4,057.
- Of the 805 content-changed blobs, two also change mode; they remain in the content-changed category and are called out separately.
- Common gitlinks: eleven identical pins and one different pin.
- Entry types do not conflict at any shared path.

## Bottom line

The official repository is the reference Flipper Zero operating environment. Momentum is a deliberately opinionated downstream distribution that keeps the same device and much of the same kernel/service foundation while transforming the user experience into a configurable field toolkit: more protocol handlers, more credential and radio workflows, more hardware-module support, more scripting APIs, themes and menu systems, persistent BLE beaconing, device-identity controls, and a very large bundled FAP ecosystem.

The cost of that transformation is equally concrete in the code: a larger exported API, a forked protobuf dependency, additional boot-time state, extensive core modifications, optional destructive settings, expanded RF boundaries, and a much wider third-party app/provenance surface. OFW is the clearer baseline; Momentum is the more capable and customizable—but also more operationally demanding—distribution.
