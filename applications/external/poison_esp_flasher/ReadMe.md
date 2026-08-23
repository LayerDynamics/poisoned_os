# Poison ESP Flasher

Poison ESP Flasher programs supported Espressif boards directly from a Flipper Zero. The
Flipper owns the UART connection, peripheral power, and boot/reset lines; the normal workflow
does not expose the attached ESP as a host USB-UART bridge.

This app is based on the GPL-3.0 ESP Flasher application maintained by 0xchocolate and the
Momentum Firmware project. Its private `esp-serial-flasher` library is provided by Espressif
under Apache-2.0. The original license files and attribution are retained in this directory.

## PoisonedOS Changes

- Uses the Flipper's fixed USART channel instead of Momentum's global UART setting.
- Accepts the `marauder_flipper` launch argument for an explicitly selected official Flipper
  WiFi Dev Board.
- Enters the ESP bootloader through the Momentum DTR/RTS/SWCLK/OTG sequence.
- Writes the standard S2 layout at `0x1000`, `0x8000`, `0xE000`, and `0x10000`.
- Calls `esp_loader_flash_verify()` after every segment and stops on an MD5 mismatch.
- Ships the pinned ESP32 Marauder v1.15.0 Flipper assets verified by
  `provenance/marauder.lock.json`.

## Build and Launch

Prepare or revalidate the pinned release cache:

```console
./fbt marauder_prepare
```

Open the on-device board-selection menu without starting an ESP write:

```console
./fbt marauder_flash ARGS="--port /dev/cu.usbmodemflip_Osprit1"
```

Start the automatic official-board flow only after that exact target has been selected:

```console
./fbt marauder_flash ARGS="--port /dev/cu.usbmodemflip_Osprit1 --target flipper-zero-wifi-dev-board"
```

The host command verifies and uploads the FAP and four Marauder segments before launching the
app. A requested serial device must enumerate as Flipper USB `0483:5740`; unrelated serial
devices, including DisplayLink `17e9:6000`, are rejected.

## Safety and Current Scope

Omitting `--target` is the safe response to board-profile ambiguity: the app opens its explicit
hardware menu and performs no write until a selection is made on the Flipper. Automatic mode
currently accepts only `flipper-zero-wifi-dev-board`; other manifest targets are rejected rather
than inferred from chip family, flash size, or enclosure appearance.

The imported manual-flash and other quick-flash options remain available for expert use. Their
assets are independent of the v1.15.0 automatic Flipper-board contract and must not be described
as automatically selected or verified by `scripts/marauder.py`.
