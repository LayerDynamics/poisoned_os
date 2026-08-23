# Poisoned_Os Web Installer

`@poisonedos/web-installer` installs Poisoned_Os on a Flipper Zero directly from a compatible Chromium browser. It is a standalone static tool: it neither imports the firmware build nor requires the dashboard runtime, Wi-Fi Dev Board, Rust bridge, account, or hosted service while installing.

The installer accepts the normal target-7 update `.tgz`, validates bounded gzip/tar structure and every `update.fuf` reference, identifies the USB device through Flipper protobuf RPC, checks SD-card reserve, uploads in 512-byte chunks, reads every file back by SHA-256, starts the device updater, reconnects for up to ten minutes, and reports success only after the returned device identifies as PoisonedOS on hardware target 7.

## Run Locally

Requirements are Node.js `^20.19.0` or `>=22.12.0`, pnpm `10.32.1`, and current Chrome, Edge, or another Chromium browser with Web Serial. Close qFlipper and other serial clients before connecting.

```console
pnpm --dir web-installer install --frozen-lockfile
pnpm --dir web-installer dev
```

Open the localhost URL printed by Vite and choose `dist/f7-C/flipper-z-f7-update-poisonedos.tgz`. Local selection validates the archive's structure, target, transfer, and returned device identity, but does not authenticate the archive's publisher. Use only a package you built from reviewed source until signed publication is configured.

## Verify and Build

```console
pnpm --dir web-installer verify
```

The output is a static site in `web-installer/dist/`. Its relative asset paths support a repository-scoped GitHub Pages URL or another HTTPS static host.

## Published Releases

A distribution build sets `VITE_POISON_RELEASE_FEED_URL` to a same-site feed and `VITE_POISON_RELEASE_KEYS` to a JSON map of trusted key IDs to SPKI P-256 public-key PEMs. The client rejects an unknown key, altered signed manifest, unsafe URL, non-target-7 release, oversized package, byte-count mismatch, or SHA-256 mismatch before archive parsing.

`tools/release/build_web_installer_feed.py` constructs the feed only after independently checking the same signed manifest and exact `.tgz` with OpenSSL. `.github/workflows/web-installer.yml` performs that check, requires the release tag to match the signed version, and publishes to GitHub Pages only for a published release while `POISON_RELEASE_DISTRIBUTION_APPROVED` is `true`. The Railway deployment uses the repository `Dockerfile` to build the target-7 updater package, sign a developer-channel manifest, and serve `releases.json`, `installer-config.json`, and the immutable package from the same origin. Railway's `POISON_RELEASE_PRIVATE_KEY_B64` is a developer deployment secret; it is not the commissioned production signing authority, so stable publication remains disabled until the production key and recorded licensing blockers are resolved.

## Recovery Behavior

For a first install from stock or another firmware, the package is placed at `/ext/update/poison-lkg`, which seeds the bootstrap last-known-good location expected by PoisonedOS. A cable update from an already identified PoisonedOS device uses a digest-specific staging directory so the current last-known-good bundle is not overwritten before activation. Failures before reboot remove only the active staging path; after reboot the installer waits for the real USB device to return and does not claim completion without a PoisonedOS identity.
