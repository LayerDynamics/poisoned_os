# Poisoned_Os Physical HIL Rack

This rack validates Poisoned_Os on two real Flipper Zero devices. The `test` role measures normal installation and boot behavior; the `recovery` role proves that a device deliberately placed in STM32 DFU can be restored over a dedicated SWD probe. These are physical end-to-end checks, not simulated or mocked device tests.

## Required Hardware

- Two Flipper Zero devices with distinct USB serial numbers.
- Two known microSD cards, each containing its role-specific sentinel file.
- A USB hub whose individual downstream ports can be controlled by `uhubctl`.
- A Black Magic Probe/Flipper Wi-Fi Devboard in USB mode or an ST-Link wired to the recovery Flipper's SWD pads.
- Direct host access to the Flipper CDC ports, the STM32 DFU interface, and the SWD probe.

The rack host must provide `uhubctl`, `dfu-util`, Python, and the pinned FBT toolchain. Before a physical suite starts, the runner re-executes itself once with the FBT Python interpreter so its child RPC commands use the repository-pinned pyserial and protobuf packages on macOS, Linux, and Windows. `POISON_HIL_PYTHON` can select an equivalent executable interpreter when the toolchain is installed at a nonstandard location.

Serial enumeration first uses pyserial from that Python environment, then another copy bundled in an installed FBT toolchain. If neither copy is importable, the runner invokes the repository's Rust `poisoned-bridge list-usb-serial-json` probe, which uses `tokio-serial`; this fallback requires Cargo. Every provider must report USB VID/PID and serial number, and the runner accepts only the Flipper runtime identity `0483:5740` plus the exact inventory serial. DisplayLink `17e9:6000` and DFU `0483:df11` can never resolve as normal-mode targets.

`tools/hil/inventory.json` is intentionally ignored by Git because it contains machine- and device-specific identifiers; it must not contain passwords, tokens, or private keys.

## Inventory Setup

Copy the committed template and replace every `REPLACE_...` value and all-zero digest:

```bash
cp tools/hil/inventory.example.json tools/hil/inventory.json
python3 tools/hil/run_suite.py --validate-only
```

The two SD sentinels must already exist on their respective cards. Calculate each SHA-256 from the exact bytes placed on the card and record the lowercase digest in the private inventory. The runner reads the file back through the real Flipper CLI and verifies its digest; it never formats or rewrites the SD card.

`usbPower.location` and `usbPower.port` are the values reported by `uhubctl`. `recovery.dfuSerial` is the serial printed by `dfu-util --list` for that Flipper in DFU mode. `recovery.probeSerial` selects the dedicated SWD probe so another attached target cannot be flashed accidentally.

## Suites

Run the production baseline suite with:

```bash
python3 tools/hil/run_suite.py --suite baseline
```

It performs the following physical workflow:

1. Builds production firmware, updater, and resources.
2. Resolves both normal-mode devices by exact serial number.
3. Installs production firmware on each device through the normal updater.
4. Verifies hardware target 7, the internal `PoisonedOS` firmware-origin identifier, firmware version, and the known SD sentinel.
5. Power-cycles both independently, measures five test-role reboots and one recovery-role reboot, and proves each CLI returns.
6. Leaves the test role idle on its main screen for 60 seconds, captures `top 0`, and records thread count, heap totals, minimum free heap, maximum block, and peak heap used since boot.
7. Sends only the recovery role to STM32 DFU, verifies its exact DFU serial, restores production firmware through its exact SWD probe, power-cycles it, and repeats the identity/storage probe.

Run physical firmware units with:

```bash
python3 tools/hil/run_suite.py --suite firmware-units
```

That suite builds `FIRMWARE_APP_SET=unit_tests`, installs it on both roles, invokes the existing `scripts/testops.py run_units` CLI workflow on each real device, retains separate transcripts, and restores production firmware in a `finally` path even when a unit test fails.

Run the authenticated JavaScript source workflow through an explicitly named Wi-Fi board:

```bash
POISON_HIL_WIFI_BOARD_ID=field \
POISON_WIFI_BOARDS='{"field":"http://blackmagic.local"}' \
  python3 tools/hil/run_suite.py --suite javascript-workflow
```

Playwright starts the production Node.js runtime at `http://127.0.0.1:4173`. The suite resolves the inventory `test` device by exact serial number for fixture safety, then the browser selects the explicit Wi-Fi board ID; it imports a digest-locked dependency, runs it on-device, verifies console output, finalizes an artifact, disconnects, restores the browser revision after pairing again, and reruns. It does not flash firmware. The complete setup and pass contract are in `docs/testing/m4-javascript-protocol.md`.

Results are written beneath the ignored `tools/hil/results/` directory. Each suite result records commands, exit codes, elapsed time, output digests, discovered ports, and physical observations. Probe and unit transcripts remain local because they may include device identifiers.

## Safety and Failure Handling

The runner rejects missing roles, duplicate serials, template placeholders, unknown inventory fields, unsafe SD paths, unsupported recovery drivers, and ambiguous USB enumeration before flashing. It never invokes qFlipper Repair or formats storage. If SWD recovery fails, leave the recovery device powered and inspect the retained result before touching cabling; do not substitute a manual button sequence and report the run as passing.

The committed host validation tests are unit tests for the inventory and transport helpers. Only a successful suite execution on the commissioned rack counts as HIL/E2E evidence for that suite's documented physical operations.
