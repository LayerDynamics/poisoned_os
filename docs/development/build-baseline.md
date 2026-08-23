# PoisonedOS Build Baseline

This document records the reproducible PoisonedOS firmware baseline for Flipper Zero hardware target 7. The device currently enumerates on recovered official firmware 1.4.3; PoisonedOS runtime values must be recaptured after the next verified installation.

## Source and Toolchain

| Field | Verified value |
|---|---|
| Measurement date | 2026-08-21 |
| Source revision | `40c50f5528b091dcb71acfb2fc5acacf48c1bfd1` plus the uncommitted M0 Task 6 and built-in UI changes |
| Product-source SHA-256 | `564e9f3c4a697c7fe0550ab0487098f4efbae3dfc60343e2ed6a5eb588e9a1f8` |
| Hardware target | `7` |
| FBT bundle | `39`, `arm64-darwin` |
| Toolchain archive SHA-256 | `d6c6fc35607af9aa357d549d29f230799c91b973b938ce776e55e1a8b99daea0` |
| Python | 3.11.9 |
| SCons | 4.7.0 |
| GNU Arm compiler | GCC 12.3.1 |
| GNU Arm linker and objcopy | Binutils 2.40.0.20230627 |
| Protocol Buffers compiler | 3.21.7 |
| Firmware API | 88.3 |

`toolchains/manifest.lock.json` is authoritative for individual executable and build-input digests. Verify it before using any baseline result:

```console
python3 tools/verify_toolchain.py
```

Expected result:

```text
toolchain verification passed: bundle 39, 6 tools, 9 build inputs
```

## Build and Reproducibility Method

The baseline build command is:

```console
./fbt firmware_all updater_all resources
```

`tools/check_reproducible_build.py` copies the locked product tree into two clean temporary roots with different absolute paths, creates the same deterministic Git metadata in each root, builds both roots with the pinned toolchain and `SOURCE_DATE_EPOCH=946684800`, and byte-compares firmware, updater, resources, API-table, and linker-map artifacts. The compiler maps each checkout path to `.` with `-ffile-prefix-map` so source-root paths do not enter payload bytes. No unexplained output byte is normalized.

Run the independent-build gate with:

```console
python3 tools/check_reproducible_build.py
```

Last verified result before the on-device Marauder flasher port:

```text
reproducible build comparison passed: 1086 artifacts, tree sha256 0a977fb50f8d6b4c1098d974b3a59d08cf4e1b442591017c37a9277d5eb58060
```

The on-device flasher and external-app resource distribution changes invalidate that artifact
count and tree digest. A fresh independent-build result is required before the current tree is
declared reproducible.

## Firmware Footprint

The values below come from `build/f7-firmware-D/firmware.elf` using the pinned `arm-none-eabi-size -A` and the linker-generated `.free_flash` section.

| Metric | Bytes | Method |
|---|---:|---|
| Interrupt vector | 316 | `.isr_vector` |
| Executable code | 631,448 | `.text` |
| Read-only data | 168,816 | `.rodata` |
| Initialized static RAM | 624 | `.data` runtime size |
| Zero-initialized and shared static RAM | 9,728 | GNU size `bss`, comprising `.bss`, `MAPPING_TABLE`, `MB_MEM1`, `MB_MEM2`, and the linker marker |
| Total static RAM | 10,352 | Initialized static RAM plus zero-initialized/shared static RAM |
| Reserved dynamic heap/stack arena | 191,232 | `._user_heap_stack`; not counted as static RAM |
| Free application flash | 247,352 | `.free_flash` |
| Raw firmware binary | 801,008 | File size of `firmware.bin` |
| DFU container | 801,317 | File size of `firmware.dfu` |

GNU size reports `text=800588`, but that aggregate combines executable code with read-only data and metadata. The section-level values above avoid presenting that aggregate as executable code.

## Compiler and Linker Configuration

The common compiler configuration is Cortex-M4 Thumb code with hard-float ABI and FPv4-SP-D16, warnings promoted to errors, per-function and per-data sections, checkout-path mapping, single-precision constants, no math `errno`, and DWARF debug information. C uses GNU C2x; C++ uses C++20 with RTTI, exceptions, thread-safe statics, and C++ atexit support disabled.

The default firmware configuration uses `DEBUG=True`: firmware application objects use `-Og`, libraries normally use `-Os`, and the core application library uses `-Og`. Linking uses newlib-nano, garbage-collects unused sections, emits a linker map, wraps allocator entry points, and uses the target-7 flash linker script. `site_scons/cc.scons`, `site_scons/firmwareopts.scons`, and `firmware.scons` are the authoritative configuration files and are digest-locked by the toolchain manifest.

## Curated Marauder Inputs

`provenance/marauder.lock.json` pins ESP32 Marauder stable v1.15.0 to source commit
`6e375e377abb70084720484e9b25de485627f688`. The official installer bundle SHA-256 is
`3b09b98e6d11a954647df8664e3d2b17e8e003adca5fdeb79026f345da4cc782`; the downloaded
bundle in `dist/marauder/v1.15.0` matches it. Prepare or revalidate the cache with:

```console
./fbt marauder_prepare
```

The normal provisioning command builds and uploads `Poison ESP Flasher`, uploads the four
size- and SHA-256-verified Flipper WiFi Dev Board segments, and launches the flasher on the
Flipper. With no target argument it opens the on-device board-selection menu and does not write
the ESP. Supplying the exact official-board target starts the fixed-address on-device flow;
the embedded Espressif library verifies every written segment with target-side MD5 before the
board is reset:

```console
./fbt marauder_flash ARGS="--port /dev/cu.usbmodemflip_Osprit1"
./fbt marauder_flash ARGS="--port /dev/cu.usbmodemflip_Osprit1 --target flipper-zero-wifi-dev-board"
```

The host-esptool bridge remains available only as the explicit `bridge-flash` diagnostic
subcommand in `scripts/marauder.py`; `marauder_flash` does not call it. Automatic flashing of
any other manifest target is rejected rather than inferred from chip family or enclosure.
Physical execution and post-flash validation are still pending.

## Hardware Runtime Baseline

The Flipper recovered successfully through official 1.4.3 DFU, was then installed with
PoisonedOS commit `40c50f55-dirty`, and currently enumerates as
`/dev/cu.usbmodemflip_Osprit1`. Three controlled `power reboot` cycles re-enumerated the same
descriptor in approximately 11 seconds and accepted CLI commands after every boot. The radio
stack and enclave remained valid. The reinserted `Flipper SD` card is mounted as EXFAT with
approximately 31.2 GB free.

| Metric | Current PoisonedOS sample | Remaining baseline requirement |
|---|---:|---|
| Current heap used | 51,784 bytes | Post-reboot PoisonedOS sample: 190,208 total minus 138,424 free. |
| Peak heap used | Not captured | Recapture after 60 seconds idle on the PoisonedOS main screen. |
| Task count | Not captured | Recapture after 60 seconds idle on the PoisonedOS main screen. |
| Boot time | Approximately 11 seconds in three cycles | Complete five-cycle measurement and report median/range from VCP disconnect to accepted `device_info`. |

The current values prove the installed PoisonedOS build survives repeated boots and retains
radio, enclave, CLI, and SD availability. They do not satisfy the final runtime baseline, which
still requires the raw transcript, device role, five-cycle timing, 60-second idle heap, and task
count.
