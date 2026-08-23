<p align="center">
  <img src="docs/assets/poisoned_os.svg" width="112" alt="Poisoned_Os flask and skull mark">
</p>

# Poisoned_Os

**A firmware for the Flipper Zero that puts the whole device in your browser while the hardware stays in your pocket.**

[![Build](https://github.com/LayerDynamics/poisoned_os/actions/workflows/build.yml/badge.svg)](https://github.com/LayerDynamics/poisoned_os/actions/workflows/build.yml)
[![Lint](https://github.com/LayerDynamics/poisoned_os/actions/workflows/lint.yml/badge.svg)](https://github.com/LayerDynamics/poisoned_os/actions/workflows/lint.yml)
[![License: GPL v3](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)

Poisoned_Os is the Flipper you can use without taking it out. Leave the official Flipper Zero Wi-Fi Dev Board attached, open the dashboard on your phone or computer, and the device is there: live screen, real controls, apps, files, hardware tools, and output, all over Wi-Fi while the Flipper stays in your pocket or bag.

The `@poisonedos/dashboard` Node.js runtime is the center of the system. It serves the dashboard over HTTP or HTTPS, carries the device session over WebSocket or secure WebSocket, and reaches the Flipper through its Wi-Fi dev board. The runtime owns that board connection and gives the builder, workloads, and other local Node.js processes stable names of their own.

The Wi-Fi Dev Board is required for the dashboard, not an optional enhancement. Poisoned_Os still runs directly on the Flipper without it, but browser control depends on the board's Wi-Fi link to the local Node.js runtime.

The browser is also the workspace. Captures keep their original files, notes, checksums, tool output, and export history together. Profiles reshape the device around a job or setup. JavaScript projects go from editing to execution on the Flipper with their logs streaming back into the dashboard.

## Install from a browser

`@poisonedos/web-installer` is a standalone Web Serial installer for the first Poisoned_Os install and later cable updates. It does not require Poisoned_Os, the dashboard runtime, or the Wi-Fi Dev Board to already be running. In current Chrome, Edge, or another Chromium browser, it checks a target-7 update archive, identifies the connected Flipper Zero, copies every update file over USB RPC, reads every file back by SHA-256, starts the Flipper updater, and waits for Poisoned_Os to return before reporting success.

Until the production signing authority is commissioned and the release blockers below are resolved, run the installer locally and select the `.tgz` produced by the firmware build:

```console
pnpm --dir web-installer install --frozen-lockfile
pnpm --dir web-installer dev
```

Open the localhost URL printed by Vite, close qFlipper and every other serial client, select `dist/f7-C/flipper-z-f7-update-poisonedos.tgz`, connect the unlocked Flipper Zero, review the exact device and package, and keep the cable attached through post-install verification. Local package mode proves archive structure, target, transfer integrity, and the returned device identity; signer authentication is provided only by the commissioned published-release feed.

The release workflow can publish the same static module through GitHub Pages with an embedded P-256 public key and a cryptographically verified, digest-bound release feed. Publication is intentionally disabled while Poisoned_Os remains legally or operationally blocked from distribution. See [`web-installer/`](web-installer) for its module contract and [the release runbook](docs/runbooks/release.md) for commissioning.

## What you need

- A Flipper Zero running Poisoned_Os.
- The official Flipper Zero Wi-Fi Dev Board, attached to the Flipper and running the pinned Blackmagic image included with Poisoned_Os. The board is required for dashboard access and remote control.
- A Windows, macOS, or Linux computer running the local `@poisonedos/dashboard` Node.js runtime.
- A phone or computer with a browser that can reach the runtime over the local network.

## What you can do

- Control the full Flipper interface from a phone or computer over Wi-Fi without handling the device.
- Launch and follow NFC, LF RFID, iButton, infrared, Sub-GHz, GPIO, USB HID, BLE, serial, storage, Marauder, and ESP tools from one place.
- Move files and keep captures organized with notes, checksums, audit history, and portable exports.
- Build profiles that change the device for field work, a lab, a classroom, or simply the way you like to use it.
- Create, run, and debug JavaScript projects from the dashboard.
- Build constrained native projects with the Rust SDK and local builder.

## Build it

The firmware builds on Windows, macOS, and Linux. FBT downloads the toolchain for the host operating system and processor on its first run.

### macOS and Linux

```console
git clone --recursive https://github.com/LayerDynamics/poisoned_os.git
cd poisoned_os
./fbt firmware_all updater_all resources
```

### Windows

Run the Windows launcher from Command Prompt or PowerShell:

```powershell
git clone --recursive https://github.com/LayerDynamics/poisoned_os.git
Set-Location poisoned_os
.\fbt.cmd firmware_all updater_all resources
```

Both commands build the same firmware, updater, and resources. Neither one flashes or writes to a connected USB device.

### Dashboard

`@poisonedos/dashboard` contains both the browser application and its local Node.js runtime. It requires Node.js `^20.19.0` or `>=22.12.0` and the repository-pinned pnpm version. The same commands run on Windows, macOS, and Linux:

```console
npm install --global pnpm@10.32.1
pnpm --dir dashboard install --frozen-lockfile
pnpm --dir dashboard verify
pnpm --dir dashboard dev
```

The runtime connects to `http://blackmagic.local` by default and serves the dashboard on port `55173`. Open `http://127.0.0.1:55173` when the browser is on the same computer.

### Open it from a phone

Use HTTPS when the dashboard is opened from a phone or another computer. Configure a certificate trusted by that browser with the runtime's hostname or LAN address in its subject alternative names.

On macOS or Linux:

```console
POISON_RUNTIME_LISTEN=0.0.0.0:55173 \
POISON_RUNTIME_PUBLIC_URL=https://poisoned.local:55173 \
POISON_RUNTIME_TLS_CERT=/absolute/path/to/poisoned.local.crt \
POISON_RUNTIME_TLS_KEY=/absolute/path/to/poisoned.local.key \
pnpm --dir dashboard dev
```

On Windows PowerShell:

```powershell
$env:POISON_RUNTIME_LISTEN = "0.0.0.0:55173"
$env:POISON_RUNTIME_PUBLIC_URL = "https://poisoned.local:55173"
$env:POISON_RUNTIME_TLS_CERT = "C:\absolute\path\to\poisoned.local.crt"
$env:POISON_RUNTIME_TLS_KEY = "C:\absolute\path\to\poisoned.local.key"
pnpm --dir dashboard dev
```

Open `https://poisoned.local:55173` from the phone. The dashboard uses HTTPS for discovery and WSS for the live RPC connection.

### Wi-Fi board and local services

The browser route runs through an official Flipper Zero Wi-Fi Dev Board with the pinned Blackmagic image. Poison ESP Flasher carries that image under **GPIO → Poison ESP Flasher → Quick Flash → Flipper WiFi Devboard → Black Magic**. Configure the board on a network the Node.js host can reach and leave it attached to the Flipper.

The default board address is `http://blackmagic.local`. To name several boards, set `POISON_WIFI_BOARDS` to a JSON object:

```console
POISON_WIFI_BOARDS='{"field":"http://blackmagic.local","lab":"http://192.168.1.44"}' \
pnpm --dir dashboard dev
```

Give local Node.js processes independent names and loopback addresses with `POISON_NODE_SERVICES`:

```powershell
$env:POISON_NODE_SERVICES = '{"builder":"http://127.0.0.1:49101","workloads":"http://127.0.0.1:49102"}'
pnpm --dir dashboard dev
```

The runtime publishes each process at an authenticated, same-origin HTTP(S) and WS(S) path. Processes can also register and renew their names while the runtime is running with the admin token printed at startup.

<details>
<summary>Run the main verification checks</summary>

These Python entry points run through `uv` with Python 3.11. On macOS or Linux, enter the pinned FBT environment before running the Python suite:

```console
./fbt lint_all
./fbt firmware_all updater_all resources
source scripts/toolchain/fbtenv.sh
uv run --no-project --python 3.11 python -m unittest discover tools/tests
pnpm --dir dashboard verify
uv run --no-project --python 3.11 python tools/rust/cargo.py test --workspace --manifest-path rust-sdk/Cargo.toml
uv run --no-project --python 3.11 python tools/rust/cargo.py test --manifest-path builder/Cargo.toml
uv run --no-project --python 3.11 python tools/rust/cargo.py test --manifest-path bridge/Cargo.toml
source scripts/toolchain/fbtenv.sh --restore
```

On Windows, run the same checks from a dedicated Command Prompt:

```batch
fbt.cmd lint_all
fbt.cmd firmware_all updater_all resources
call scripts\toolchain\fbtenv.cmd env
uv run --no-project --python 3.11 python -m unittest discover tools\tests
pnpm --dir dashboard verify
uv run --no-project --python 3.11 python tools\rust\cargo.py test --workspace --manifest-path rust-sdk\Cargo.toml
uv run --no-project --python 3.11 python tools\rust\cargo.py test --manifest-path builder\Cargo.toml
uv run --no-project --python 3.11 python tools\rust\cargo.py test --manifest-path bridge\Cargo.toml
```

</details>

## Explore the code

- [`applications/`](applications) and [`lib/`](lib) contain the firmware, on-device apps, and Poisoned_Os services.
- [`dashboard/`](dashboard) contains the browser interface, local Node.js runtime, HTTPS/WS web transport, Wi-Fi board owner, and named service registry.
- [`web-installer/`](web-installer) contains the independent USB browser installer and signed release-feed client.
- [`bridge/`](bridge) contains auxiliary host-side storage, builder, USB, and Bluetooth code.
- [`rust-sdk/`](rust-sdk) and [`builder/`](builder) contain the constrained Rust toolchain path.
- [The product specification](docs/specs/SPEC-1-poisonedos-for-the-flipper-zero.md) explains the whole system; [architecture decisions](docs/decisions) explain why it is built this way.
- [The build baseline](docs/development/build-baseline.md) and [V1 requirement ledger](docs/release/v1-requirement-ledger.md) are the source of truth for what has actually been verified.

## Release status

The firmware and dashboard build from source, and the firmware has booted on real Flipper Zero hardware. Stable release packages are gated on complete physical platform qualification and resolution of two third-party licensing findings. The [known limitations](docs/release/v1-known-limitations.md) and [current requirement ledger](docs/release/v1-requirement-ledger.md) track that release evidence.

## Responsible use

Poisoned_Os is for authorized field work, research, labs, and education. Use radio, credential, USB, GPIO, and companion-board tools only on systems you own or have explicit permission to test.

## License and provenance

Poisoned_Os and its upstream firmware base are licensed under [GPLv3](LICENSE). The repository is derived from a locked official Flipper Zero firmware snapshot. Component origins, licenses, and source digests are recorded under [`provenance/`](provenance).
