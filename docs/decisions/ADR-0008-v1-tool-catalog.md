# ADR-0008: PoisonedOS V1 tool catalog boundary

## Status

Accepted as the V1 catalog boundary; individual adapters remain subject to their own firmware, policy, and hardware evidence.

## Decision

PoisonedOS keeps the existing Flipper protocol/radio engines in place and exposes them through bounded structured-app adapters. The catalog is local-only in V1: entries may come from the signed release, verified device inventory, explicitly imported packages, or an explicitly selected local repository. A catalog listing is not installable unless its package is verified, compatible, fresh, non-revoked, and its adapter is present.

The ten V1 families are NFC, LF RFID, iButton/1-Wire, infrared, Sub-GHz, GPIO, USB/HID, BLE/Bluetooth HID, serial/expansion, and storage/archive. Each family has separate observe/read and mutation/transmit capabilities. Mutations require a fresh session authorization decision and exact-target confirmation; radio actions also require current device-region policy. Classroom restrictions intersect with role and hardware policy and cannot be weakened by a profile or browser request.

## Ownership and provenance

In-tree adapters are maintained by the PoisonedOS firmware owner and must cite the upstream application/library path. First-party signed packages use the M0 release authority. Third-party packages require an explicit provenance/license review and are never implicitly promoted by appearing in a local repository. Every catalog record names its source, signer, package digest, adapter version, review owner, and update source.

## Consequences

Structured commands/results are the only browser-facing adapter boundary. Legacy device-only applications remain available through their existing UI and framebuffer path. A family with no verified adapter evidence remains unavailable rather than being documented as supported.
