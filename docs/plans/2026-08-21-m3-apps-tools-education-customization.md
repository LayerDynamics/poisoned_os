# M3 Apps, Updates, Recovery, Tools, Education, and Customization Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use lore:execute to implement this plan task-by-task.
> **Scope guard:** Do ONLY what is listed here. If you discover adjacent issues, record them in the deferred-findings section and continue. Do NOT fix them.

**Goal:** Turn PoisonedOS into a professional field and classroom platform whose signed firmware/content updates, recovery, applications, curated security tools, lessons, profiles, and interface can be managed safely from either the device or browser.
**Architecture:** Add one signed content/update transaction layer over the existing `applications/system/updater` and `lib/update_util` paths, then use it for applications, firmware, lessons, tool data, profiles, resources, and dashboard UI packs. Add a structured application protocol alongside legacy FAP/framebuffer compatibility, drive each professional tool family from a validated adapter manifest, and apply versioned device/classroom profiles transactionally through the M1 policy and confirmation services.
**Tech Stack:** C/Furi/FAP, nanopb/protobuf, the M0-approved release signature primitive, JSON Schema, TypeScript/React, Rust bridge, Vitest, Playwright, physical-device E2E.
**Practices:** Signed-by-default distribution, capability review, structured-first UX, reversible customization, safe educational defaults, deterministic catalogs, accessibility testing, approval-gated commits.
**Required skills:** `lore:execute`, `lore:test-driven-development`, `lore:testing`, `lore:security`, `lore:e2e-test-expert`, `lore:verification-before-completion`

---

## Achievement Ledger

| Achievement | Status before implementation | Required closing evidence |
|---|---|---|
| A3.1 Signed application packages | In progress: bounded package contract, deterministic host builder/signer, firmware verifier/transaction, and deterministic local-catalog foundations are implemented; device RPC/catalog wiring, lifecycle fault vectors, and full signature vectors remain. | Deterministic build/sign/verify/catalog plus transactional lifecycle and rollback pass. |
| A3.2 Signed firmware and content updates | In progress: shared bounded content-update state/rollback coordinator and firmware regression coverage are implemented; updater integration, signature vectors, health signaling, and fault injection remain. | Existing updater integration survives every interruption and failed-health boundary. |
| A3.3 Structured application control | In progress: bounded structured-app protocol, firmware sequence validator/SDK, RPC input gate, and dashboard structured/legacy view foundations are implemented; full authenticated RPC dispatch and physical dual-mode E2E remain. | Typed and legacy modes pass together with malformed-event isolation. |
| A3.4 Profiles and safe customization | In progress: versioned profile protocol, immutable built-in recovery profile, role-capability/contrast validation, atomic preview/apply/reset, asset identifier checks, and dashboard editor foundations are implemented; signed asset transactions, import/export, full settings UI, and physical recovery remain. | Field/classroom profile preview, atomic apply, export/reset/import, and accessibility pass. |
| A3.5 Curated professional tools | In progress: ADR-0008 boundary, capability contract, ten-family catalog schema/data, host validator, firmware execution gate, and dashboard catalog/run foundations are implemented; family adapters, signed inventory reconciliation, and physical proof remain. | ADR-0008 and independent proof for all ten hardware families pass. |
| A3.6 Student and educator workflows | In progress: signed lesson schema/introductory pack, firmware progress/reset state machine, digest-only evidence checkpoints, and dashboard lesson/classroom authoring, assignment, and review foundations are implemented with regression coverage; offline synchronization and physical classroom evidence remain. | Device-only lesson plus offline author/assign/collect/review workflow passes. |
| A3.7 Confirmation and recovery | In progress: the 12-caller destructive-operation inventory, recovery state machine, device-only scene entrypoints, last-known-good gating, cancellation, and user-data preservation regression coverage are implemented; caller dispatch integration, updater boot integration, and physical recovery remain. | Destructive-caller coverage and dashboard-independent firmware/profile/index recovery pass. |
| A3.8 Field and classroom E2E | Not started | Complete real-hardware package/update/tool/profile/lesson/recovery protocol passes. |

**Canonical task commands:** firmware tasks run `./fbt FIRMWARE_APP_SET=unit_tests` and `python3 tools/hil/run_suite.py --suite firmware-units`; Python package/catalog tools run `python3 -m unittest discover tools`; dashboard tasks run `pnpm --dir dashboard verify`; bridge tasks use `python3 tools/rust/cargo.py`; physical workflows run the exact HIL and Playwright commands named below. Record command, versions, signer/fixture/device IDs, exit code, and evidence digest before changing a ledger row.

**Implementation evidence (Task 4 foundation):** `poison_content_update.h/.c` defines the shared `Discovered → Receiving → Staged → Verified → AwaitingConfirmation → Activating → Healthy/RolledBack/Quarantined` transition contract, validates bounded identifiers/digests, and preserves a previous digest for rollback. `poison_content_update_test.c` covers ordered activation, invalid inputs, rollback, and terminal-state rejection; the unit firmware image passed on 2026-08-21. Existing updater wiring, signed manifest verification, boot-health persistence, and interruption fault vectors remain open.

**Implementation evidence (Tasks 19–20 foundation):** `data/poison/lessons/lesson.schema.json` and `getting-started.json` define signed bounded lesson packs; `poison_lessons.c/.h` enforces ordered signed steps, digest-only checkpoints, per-learner progress, completion, and reset; `poison_assignments.c/.h` adds bounded workspace-scoped assignment creation, instructor-only binding/reset, role-checked collection, and listing; `dashboard/src/education/` provides lesson execution, classroom assignment, authoring validation/versioning, digest-only progress review, and deterministic offline assignment queue/merge helpers with regression tests. Firmware unit build and dashboard verification pass; signed synchronization transport/export and physical device-only evidence remain open.

**Implementation evidence (Task 22 foundation):** `applications/system/poison_recovery/` provides a dashboard-independent recovery state machine with menu, firmware/profile/index operations, last-known-good gating, cancellation, and mandatory user-data preservation; `poison_recovery_test.c` covers successful restore, unsafe metadata denial, and terminal cancellation. Updater boot-time fallback, signed artifact discovery, and physical recovery evidence remain open.

**Implementation evidence (Task 21 foundation):** `poison_confirmation.h/.c` now enumerates storage, evidence, migration, package, update, radio-policy, identity, native-code, application, profile, lesson, and recovery callers; the firmware regression test requires a confirmation policy row for every enumerated caller. Generic RPC caller dispatch and changed-argument mutation coverage remain open.

## Achievement A3.1: Signed Application Packages

### Task 1: Define and verify the package format

**Files:**
- Create: `assets/protobuf/poison_packages.proto`
- Create: `assets/protobuf/poison_packages.options`
- Generate: `dashboard/src/generated/poison-packages.ts` through the M0 protocol generator
- Generate: `bridge/src/generated/poison_packages.rs` through the M0 protocol generator
- Create: `schemas/poison/package-manifest.schema.json`
- Create: `applications/services/poison_packages/poison_package_verify.c`
- Create: `applications/services/poison_packages/poison_package_verify.h`
- Create: `tools/packages/build_package.py`
- Create: `tools/packages/sign_package.py`
- Test: `applications/debug/unit_tests/tests/packages/poison_package_verify_test.c`
- Test: `tools/packages/tests/test_package_reproducibility.py`

**Contract:**

```json
{
  "packageFormat": 1,
  "contentType": "application",
  "id": "org.poisonedos.example",
  "version": "1.2.3",
  "firmwareApi": ">=1.0.0 <2.0.0",
  "payloads": [{"path": "app.fap", "sha256": "64-lowercase-hex-characters"}],
  "entrypoint": "app.fap",
  "capabilities": ["storage.project.read"],
  "contentSha256": "64-lowercase-hex-characters",
  "signingKeyId": "release-key-identifier"
}
```

**Steps:**

1. Write failing tests for canonical manifests, signature vectors, hash mismatch, unknown key, revoked key, downgrade, path traversal, duplicate members, decompression bounds, incompatible firmware API, and unsupported content type.
2. Define a deterministic archive ordering and canonical manifest encoding for application, lesson, tool-data, theme, icon/font, menu, and UI-pack content.
3. Implement host packaging/signing and device verification using the signature primitive approved in M0; do not introduce a second trust system.
4. Run firmware and Python package tests twice from clean outputs → Expected: identical package digests and complete rejection coverage.
5. Stage diffs and request approval for proposed `feat(packages): add signed deterministic application bundles`.

**Implementation evidence (Task 1 foundation):** `schemas/poison/package-manifest.schema.json`, generated `poison_packages.proto` bindings, `tools/packages/build_package.py`, `tools/packages/sign_package.py`, and `poison_package_verify.c` establish bounded package types, path-safe payload validation, deterministic ZIP member ordering/timestamps, lowercase SHA-256 checks, signer revocation/downgrade rejection, and the M0 OpenSSL signing primitive. Reproducibility tests and the firmware unit image passed on 2026-08-21; complete signature/key vectors, device archive verification, and transactional lifecycle remain open.

### Task 2: Implement install, update, rollback, and removal

**Files:**
- Create: `applications/services/poison_packages/application.fam`
- Create: `applications/services/poison_packages/poison_packages.c`
- Create: `applications/services/poison_packages/poison_packages.h`
- Create: `applications/services/poison_packages/poison_package_transaction.c`
- Create: `applications/services/rpc/rpc_poison_packages.c`
- Create: `dashboard/src/packages/PackageManager.tsx`
- Test: `applications/debug/unit_tests/tests/packages/poison_package_transaction_test.c`
- Test: `dashboard/src/packages/PackageManager.test.tsx`

**Steps:**

1. Test install/update/disable/enable/remove/rollback/quarantine state transitions, storage exhaustion, power loss, revocation, incompatible versions, and protected packages.
2. Stage packages in VFS transactions, verify before activation, retain one known-good version, and audit every state change.
3. Require explicit device confirmation for new high-risk capabilities and display signer, version, permissions, provenance, and installed/available/running/incompatible/disabled/quarantined state in the browser.
4. Run package, VFS, RPC, and dashboard tests → Expected: failed updates leave the prior verified version launchable.

### Task 3: Add deterministic package discovery and local catalogs

**Files:**
- Create: `applications/services/poison_packages/poison_package_catalog.c`
- Create: `applications/services/poison_packages/poison_package_catalog.h`
- Create: `applications/services/rpc/rpc_poison_package_catalog.c`
- Create: `bridge/src/packages/catalog.rs`
- Create: `dashboard/src/packages/PackageSources.tsx`
- Test: `applications/debug/unit_tests/tests/packages/poison_package_catalog_test.c`
- Test: `bridge/tests/package_catalog.rs`

**Steps:**

1. Define installed, staged, available, incompatible, disabled, quarantined, revoked, and rollback-candidate records with signer, source, digest, version, compatibility, and capabilities.
2. Index verified packages from device storage, the signed catalog bundled with the release, explicitly imported local package files, and explicitly selected local repository directories. Keep remote catalog transport as a disabled interface extension for post-V1 FR-61 unless ADR-0001 explicitly brings it into V1 scope; never equate a listing with an installable verified package.
3. Cache signed catalog metadata for offline browsing, retain source and freshness state, and reject rollback/downgrade/revocation conflicts before download or transfer.
4. Test duplicate sources, stale metadata, source disappearance, conflicting package IDs, revoked metadata keys, offline restart, and catalog/package inventory reconciliation.
5. Run firmware, bridge, and dashboard catalog tests → Expected: every displayed availability state is derived from current signed metadata and local verified inventory.

**Implementation evidence (Task 3):** `poison_package_catalog.c/.h`, `bridge/src/packages.rs`, `PackageSources.tsx`, and their tests provide bounded records for device, bundled-release, imported-file, and local-repository sources; deterministic ordering; duplicate-source and conflicting-digest rejection; stale/missing-source quarantine; revoked/unverified metadata exclusion; and JSON cache restore for offline browsing. `rpc_poison_package_catalog_list_authenticated` validates the M1 session and fixed `package-catalog` channel before returning the ordered bounded device catalog, with firmware regression coverage for valid and wrong-channel requests. `PackageCatalog::reconcile_source_paths` now reconciles a fresh inventory and quarantines disappeared records, with bridge regression coverage. Remote catalog transport remains disabled; signed catalog-key vectors and complete downgrade/revocation fixtures remain open.

## Achievement A3.2: Signed Firmware and Content Updates

### Task 4: Extend the existing updater through one verified transaction model

**Files:**
- Create: `docs/decisions/ADR-0011-content-update-transaction.md`
- Modify: `applications/system/updater/application.fam`
- Modify: `applications/system/updater/util/update_task.c`
- Modify: `applications/system/updater/util/update_task.h`
- Modify: `lib/update_util/update_manifest.c`
- Modify: `lib/update_util/update_manifest.h`
- Create: `applications/services/poison_packages/poison_content_update.c`
- Create: `applications/services/poison_packages/poison_content_update.h`
- Create: `applications/services/rpc/rpc_poison_content_update.c`
- Create: `bridge/src/update.rs`
- Create: `dashboard/src/settings/UpdateManager.tsx`
- Test: `applications/debug/unit_tests/tests/update/poison_content_update_test.c`
- Test: `bridge/tests/update.rs`

**Steps:**

1. Record the current `.fuf` manifest, staged loader, firmware/radio/resources, `.fupdate` pointer, internal backup, and boot-time activation behavior before changing it.
2. In ADR-0011, define the shared state machine `Discovered → Downloading/Receiving → Staged → Verified → AwaitingConfirmation → Activating → Healthy/RolledBack/Quarantined`, protected targets, atomicity boundary, boot health signal, and compatibility rules for firmware, application, lesson, tool-data, theme, font/icon, menu, resource, and UI-pack content.
3. Write failing tests for every state transition plus tamper, wrong hardware target, incompatible API, downgrade, revoked signer, insufficient storage, duplicate activation, cancellation, power loss at each write boundary, failed post-boot health, and missing rollback artifact.
4. Refactor the Task 2 application-package transaction behind the shared coordinator so application and non-application content use one verification/activation/rollback vocabulary, then preserve the current Flipper update path behind that coordinator; do not fork firmware flashing or application updates into unrelated state machines.
5. Require the M1 exact-target confirmation token for firmware, boot/resource, protected-package, trust-root, and rollback operations; audit discovery through final health or rollback.
6. Expose bounded authenticated RPC and bridge/dashboard controls for import, stage, verify, inspect, activate, health, cancel, rollback, and quarantine.
7. Extend the M1 diagnostic registry with bounded package verification/revocation, update stage/health/rollback, and recovery counters; prove manifests, paths, signer material, and package contents are redacted.
8. Run updater, package, storage-fault, policy, bridge, and dashboard tests → Expected: interrupted or unhealthy activation returns to the previous verified state and never marks an unverified candidate healthy.

## Achievement A3.3: Structured Application Control

### Task 5: Add the structured application protocol without breaking legacy apps

**Files:**
- Create: `assets/protobuf/poison_app.proto`
- Create: `assets/protobuf/poison_app.options`
- Generate: `dashboard/src/generated/poison-app.ts` through the M0 protocol generator
- Generate: `bridge/src/generated/poison_app.rs` through the M0 protocol generator
- Create: `applications/services/poison_app/poison_app.h`
- Create: `applications/services/poison_app/poison_app.c`
- Create: `applications/services/poison_app/poison_app_sdk.h`
- Create: `applications/services/rpc/rpc_poison_app.c`
- Create: `dashboard/src/apps/StructuredAppView.tsx`
- Create: `dashboard/src/apps/LegacyFramebufferView.tsx`
- Test: `applications/debug/unit_tests/tests/apps/poison_app_protocol_test.c`
- Test: `dashboard/src/apps/StructuredAppView.test.tsx`

**Steps:**

1. Test typed commands, forms, progress, logs, tables, artifacts, cancellation, schema versioning, malformed events, and event ordering.
2. Expose a bounded SDK for structured events and commands; authorize commands through the M1 session policy.
3. Select structured rendering when advertised and preserve existing framebuffer/input behavior for legacy FAPs.
4. Run firmware and dashboard tests → Expected: both modes work and a malformed structured app cannot crash or control the dashboard shell.

**Implementation evidence (Task 5 foundation):** `poison_app.proto/.options` (union tags 92–93), generated C/TypeScript/Rust bindings, `poison_app.c/.h`, `poison_app_sdk.h`, `rpc_poison_app.c`, `StructuredAppView.tsx`, and `LegacyFramebufferView.tsx` provide bounded typed event payloads, contiguous sequence enforcement, terminal cancellation, command-size validation, and a structured/legacy rendering split. Firmware unit and dashboard tests pass; authenticated command dispatch, complete typed form/table/artifact handling, and physical dual-mode E2E remain open.

## Achievement A3.4: Profiles and Safe Customization

**Current implementation status (2026-08-21):** In progress. The firmware now contains an
immutable built-in PoisonedOS field-console presentation across the desktop home, app and
settings navigation, lock/control/PIN-delay surfaces, status rail, diagnostics, updater, and
power lifecycle. The source regression suite is `tools/tests/test_poison_ui.py`; the current
firmware build passes with API 88.3. Transactional user profiles, signed appearance packs,
device/dashboard preview, atomic activation, rollback, import/export, accessibility validation,
and device-only known-good profile recovery in Task 6 remain unimplemented and are not implied
by this built-in visual foundation.

### Task 6: Implement transactional profiles and appearance settings

**Files:**
- Create: `assets/protobuf/poison_profiles.proto`
- Create: `assets/protobuf/poison_profiles.options`
- Generate: `dashboard/src/generated/poison-profiles.ts` through the M0 protocol generator
- Generate: `bridge/src/generated/poison_profiles.rs` through the M0 protocol generator
- Create: `applications/services/poison_profiles/application.fam`
- Create: `applications/services/poison_profiles/poison_profiles.c`
- Create: `applications/services/poison_profiles/poison_profiles.h`
- Create: `applications/services/poison_profiles/poison_profile_assets.c`
- Create: `applications/services/poison_profiles/poison_profile_assets.h`
- Create: `applications/services/rpc/rpc_poison_profiles.c`
- Create: `applications/settings/poison_settings/application.fam`
- Create: `applications/settings/poison_settings/poison_settings.c`
- Create: `applications/settings/poison_settings/poison_settings_scenes.c`
- Create: `dashboard/src/customization/ProfileEditor.tsx`
- Create: `dashboard/src/customization/ThemeEditor.tsx`
- Test: `applications/debug/unit_tests/tests/profiles/poison_profiles_test.c`
- Test: `dashboard/src/customization/ProfileEditor.test.tsx`

**Steps:**

1. Define versioned profiles for role/policy, enabled tools and visibility, favorites, menus, shortcuts, home/status presentation, lock behavior, theme, font/icon pack, notification/haptics, dashboard layout, tool defaults, transport policy, logging policy, evidence policy, radio region, peripheral safety, and classroom restrictions.
2. Implement device and dashboard preview for every visible setting plus a field-by-field change/consequence summary before one atomic apply.
3. Verify every theme/icon/font/menu asset package, identifier/reference, size, glyph/icon bound, contrast rule, target/API version, signature, and capability before activation; retain an immutable built-in known-good UI profile that cannot be removed.
4. Test preview, validation, atomic apply, cancellation, corrupt/incompatible/missing assets, inaccessible contrast, power loss, rollback, import, export, and device-only recovery.
5. Forbid a profile from granting capabilities unavailable to the role or weakening hardware/region policy; a failed preview/activation must leave the prior profile and UI launchable.
6. Run profile, accessibility, package, and storage-fault tests → Expected: invalid or interrupted activation never prevents recovery into the known-good profile.

**Achievement check:** An owner can configure a field profile and an instructor can configure a constrained classroom profile, export either, reset the device, and restore it exactly.

**Implementation evidence (Task 6 foundation):** `poison_profiles.proto/.options` (union tags 94–96), generated bindings, `poison_profiles.c/.h`, `poison_profile_assets.c/.h`, `rpc_poison_profiles.c`, `ProfileEditor.tsx`, `ThemeEditor.tsx`, and tests implement versioned profile fields, immutable `poisonedos.field-console` recovery initialization, role capability intersection, minimum contrast validation, preview-before-apply, atomic apply/reset, bounded asset identifiers, and dashboard change summaries. Signed asset verification, persistent storage transactions, import/export, complete settings scenes, and device-only recovery E2E remain open.
`ProfileEditor.tsx` now also provides canonical format-1 JSON export/import with capability-mask preservation and validation-backed rejection of malformed or inaccessible profiles; dashboard verification passes. Firmware-signed asset transactions, persistent storage transactions, complete settings scenes, and device-only recovery E2E remain open.

## Achievement A3.5: Curated Professional Tool Catalog

### Task 7: Decide the supported tool sources and contribution boundary

**Files:**
- Create: `docs/decisions/ADR-0008-v1-tool-catalog.md`
- Create: `docs/security/tool-capability-policy.md`
- Create: `docs/development/tool-adapter-contract.md`

**Steps:**

1. Inventory the OFW-derived applications and libraries used by NFC, LF RFID, iButton/1-Wire, infrared, Sub-GHz, GPIO, BadUSB/USB HID, Bluetooth HID/BLE, serial/expansion, archive, and storage workflows.
2. Decide which tools are maintained in-tree, packaged first-party, approved third-party, or unsupported; record license, provenance, review ownership, update source, and vulnerability response for each class.
3. Define the adapter boundary between existing applications and PoisonedOS structured commands/results so tool integration does not duplicate radio/protocol engines.
4. Define capability, regional-policy, classroom-policy, destructive-action, raw-capture, and evidence-redaction rules per family.
5. Review ADR-0008 before implementing catalog adapters; unresolved ownership or licensing blocks the affected family, not the accuracy of the catalog.

**Implementation evidence (Task 7 foundation):** `ADR-0008-v1-tool-catalog.md`, `tool-capability-policy.md`, and `tool-adapter-contract.md` establish the local-only source boundary, ten hardware families, provenance/ownership fields, separate observe/mutation capabilities, current-device authorization, resource release, and evidence rules. They use domain ownership and authorization terms rather than milestone labels.

### Task 8: Define and validate the curated tool catalog

**Files:**
- Create: `data/poison/catalog/tool.schema.json`
- Create: `data/poison/catalog/tools.json`
- Create: `tools/catalog/validate_catalog.py`
- Create: `applications/services/poison_tools/poison_tools.c`
- Create: `applications/services/poison_tools/poison_tools.h`
- Create: `applications/services/poison_tools/application.fam`
- Create: `applications/services/rpc/rpc_poison_tools.c`
- Create: `dashboard/src/tools/ToolCatalog.tsx`
- Create: `dashboard/src/tools/ToolRunView.tsx`
- Test: `tools/catalog/tests/test_catalog.py`
- Test: `dashboard/src/tools/ToolRunView.test.tsx`

**Steps:**

1. Require each catalog entry to declare purpose, authorization guidance, hardware, capabilities, parameters, structured output, evidence types, safety notes, and a non-destructive sample.
2. Seed the catalog only from adapters completed in Tasks 9–18, covering the hardware-supported NFC, LF RFID, iButton, infrared, Sub-GHz, GPIO, USB/HID, BLE, serial, and storage workflows required by FR-26; a missing category blocks the milestone instead of being documented as available.
3. Validate unique IDs, schema versions, package references, capability declarations, bounded parameters, output schemas, and applicable radio-region policy in CI; profiles and tool defaults cannot weaken regulatory enforcement without an explicit authorized developer policy.
4. Route execution through structured app sessions and save selected results through the M2 evidence service.
5. Run catalog, structured-app, policy, and dashboard tests → Expected: catalog and installed package inventory agree exactly.

**Implementation evidence (Task 8 foundation):** `data/poison/catalog/tool.schema.json`, `tools.json`, `tools/catalog/validate_catalog.py`, `poison_tools.c/.h`, `rpc_poison_tools.c`, `ToolCatalog.tsx`, `ToolRunView.tsx`, and tests enforce unique bounded records for all ten families and expose foundation entries as non-runnable until a verified adapter is present. The catalog validator and firmware/dashboard checks pass; family-specific adapters, signed inventory reconciliation, and physical evidence remain open.

The ten tasks below share this completion protocol: first preserve current device-only behavior and file-format fixtures in compatibility tests; then define bounded typed commands, sequence/credit-controlled events, cancellation, hardware ownership, capabilities, and raw/derived evidence schemas; enforce policy in firmware; preserve a device UI and stop/recovery path; finally run allowed, denied, cancel, disconnect/reconnect, contention, and evidence-verification scenarios on physical hardware through `python3 tools/hil/run_suite.py --suite tool-families`.

### Task 9: Integrate NFC workflows

**Files:**
- Modify: `applications/main/nfc/application.fam`
- Create: `applications/services/poison_tools/adapters/poison_tool_nfc.c`
- Create: `applications/services/poison_tools/adapters/poison_tool_nfc.h`
- Create: `dashboard/src/tools/families/NfcTool.tsx`
- Test: `applications/debug/unit_tests/tests/tools/poison_tool_nfc_test.c`
- Test: `dashboard/src/tools/families/NfcTool.test.tsx`

**Steps:**

1. Integrate the current `applications/main/nfc` and `lib/nfc` reader/parser paths for supported tag inventory, bounded metadata parsing, raw capture, derived interpretation, and evidence submission.
2. Declare separate read, raw-capture, write, and emulation capabilities; write/emulation require explicit policy and exact-target confirmation.
3. Test malformed/oversized tag data, removal mid-read, unsupported protocol, cancel, RF ownership contention, denied mutation, and digest preservation.

### Task 10: Integrate LF RFID workflows

**Files:**
- Modify: `applications/main/lfrfid/application.fam`
- Create: `applications/services/poison_tools/adapters/poison_tool_lfrfid.c`
- Create: `applications/services/poison_tools/adapters/poison_tool_lfrfid.h`
- Create: `dashboard/src/tools/families/LfRfidTool.tsx`
- Test: `applications/debug/unit_tests/tests/tools/poison_tool_lfrfid_test.c`
- Test: `dashboard/src/tools/families/LfRfidTool.test.tsx`

**Steps:**

1. Integrate `applications/main/lfrfid` and `lib/lfrfid` for bounded read/detect, protocol/confidence output, raw capture, and evidence submission.
2. Separate read, write, and emulation capabilities and arbitrate shared GPIO/timer resources before acquisition.
3. Test noisy/unknown input, cancellation, credential redaction, denied write/emulation, hardware contention, and cleanup.

### Task 11: Integrate iButton and 1-Wire workflows

**Current implementation status (2026-08-21):** The shared iButton adapter now allocates and tears
down the upstream protocol/key objects, performs physical reads through `ibutton_protocols_read`,
returns the detected protocol and bounded rendered data, and exposes explicit write-ID and emulate
start/stop lifecycle calls. The adapter is linked against the firmware iButton library and exported
through the Poison tool service ABI. Physical read/write/emulation evidence still requires a connected
1-Wire accessory; the adapter does not claim that hardware evidence from build tests.

**Files:**
- Modify: `applications/main/ibutton/application.fam`
- Modify: `applications/main/onewire/application.fam`
- Create: `applications/services/poison_tools/adapters/poison_tool_ibutton.c`
- Create: `dashboard/src/tools/families/IButtonTool.tsx`
- Test: `applications/debug/unit_tests/tests/tools/poison_tool_ibutton_test.c`
- Test: `dashboard/src/tools/families/IButtonTool.test.tsx`

**Steps:**

1. Integrate supported key read/detect paths from both current applications and emit protocol, bounded bytes, validation result, and evidence references.
2. Gate write, emulation, and bus-power modes independently; show exact target and electrical consequence before approval.
3. Test disconnect, short/invalid key data, bus fault, cancellation, denied mutation, and repeated resource release.

### Task 12: Integrate infrared workflows

**Current implementation status (2026-08-21):** The shared infrared adapter now uses the existing
`InfraredWorker` and HAL for decoded/raw receive, bounded receive timeouts, signal metadata, steady
transmit, contention checks, and deterministic stop/cleanup. It is linked against the firmware
infrared library and exported through the Poison tool service ABI. Physical capture/transmit evidence
still requires an attached IR source/receiver and is not inferred from the build.

**Files:**
- Modify: `applications/main/infrared/application.fam`
- Create: `applications/services/poison_tools/adapters/poison_tool_infrared.c`
- Create: `dashboard/src/tools/families/InfraredTool.tsx`
- Test: `applications/debug/unit_tests/tests/tools/poison_tool_infrared_test.c`
- Test: `dashboard/src/tools/families/InfraredTool.test.tsx`

**Steps:**

1. Integrate `applications/main/infrared` and `lib/infrared` receive, decode, raw-capture, inspection, and evidence paths.
2. Treat transmit/replay as separate confirmed capabilities and enforce bounded frequency/timing/sample counts.
3. Test malformed and oversized signals, cancel, receiver/transmitter contention, denied replay, disconnect, and evidence fidelity.

### Task 13: Integrate Sub-GHz workflows and regulatory enforcement

**Current implementation status (2026-08-21):** The shared Sub-GHz adapter now initializes the
existing protocol registry, CC1101 device registry, receiver, and worker; validates frequencies
against the selected hardware; performs bounded decoded receive with RSSI/LQI metadata; supports
bounded raw LevelDuration transmit; and tears down the radio and worker deterministically. It is
linked through the Poison tool service ABI. Physical RF evidence and the full policy recheck path
still require connected-hardware verification.

**Files:**
- Modify: `applications/main/subghz/application.fam`
- Create: `applications/services/poison_tools/adapters/poison_tool_subghz.c`
- Create: `dashboard/src/tools/families/SubGhzTool.tsx`
- Test: `applications/debug/unit_tests/tests/tools/poison_tool_subghz_test.c`
- Test: `dashboard/src/tools/families/SubGhzTool.test.tsx`

**Steps:**

1. Integrate `applications/main/subghz` and `lib/subghz` frequency/config validation, receive/decode/raw capture, analyzer output, and evidence paths.
2. Evaluate hardware band, configured region, classroom profile, role, and capability immediately before every transmit/replay/analyzer action; cached browser approval cannot bypass changed device policy.
3. Test out-of-region frequencies, unsupported hardware bands, hop/replay attempts, cancel, RF contention, disconnect, and raw/derived evidence separation.

### Task 14: Integrate GPIO workflows

**Current implementation status (2026-08-21):** In progress. The shared GPIO adapter now executes
validated external-pin configuration, reads, writes, bounded sampling, and analog-safe release through
the Flipper HAL (`applications/services/poison_tools/poison_gpio_adapter.c`), with firmware unit tests.
The ESP target adapter now covers every bundled board image: the WiFi dev board, Marauder S2/S3,
FlipperHTTP S2, and Wardriver WROOM/S3. `Poison ESP Flasher` now ports
Momentum's device-resident Espressif serial-flasher architecture: the Flipper controls
DTR/RTS/SWCLK and OTG, enters the attached ESP bootloader, writes bootloader at `0x1000`,
partition table at `0x8000`, boot_app0 at `0xE000`, and firmware at `0x10000`, and performs
target-side MD5 verification after every segment. The normal `marauder_flash` host command only
verifies and stages the pinned v1.15.0 assets and FAP, then launches the on-device board menu or
the explicit `marauder_flipper` auto path; it never calls host esptool. The previous GPIO bridge
is retained as the explicit `bridge-flash` diagnostic path and always tears down through the
reserved 1200-baud signal. Requested serial ports must present the Flipper `0483:5740`
descriptor, so the DisplayLink `17e9:6000` device is rejected. The first-party `Poison Marauder`
FAP provides device information, passive AP scan, result listing, stop, bounded UART output,
and overrun markers; it deliberately exposes no transmit/attack shortcut before M1 policy and
exact-confirmation integration. The focused host suite has 18 passing tests and the on-device
flasher builds at API 88.3. The first verified board write and post-flash companion validation
remain required before this task is complete.

**Files:**
- Modify: `applications/main/gpio/application.fam`
- Modify: `applications/main/gpio/gpio_app.c`
- Modify: `applications/main/gpio/gpio_app_i.h`
- Modify: `applications/main/gpio/scenes/gpio_scene_start.c`
- Modify: `applications/main/gpio/scenes/gpio_scene_usb_uart.c`
- Modify: `applications/services/rpc/rpc_gpio.c`
- Create: `applications/external/application.fam`
- Create: `applications/external/poison_marauder/application.fam`
- Create: `applications/external/poison_marauder/poison_marauder.c`
- Create: `applications/external/poison_esp_flasher/application.fam`
- Create: `applications/external/poison_esp_flasher/esp_flasher_app.c`
- Create: `applications/external/poison_esp_flasher/esp_flasher_worker.c`
- Create: `provenance/marauder.lock.json`
- Create: `scripts/marauder.py`
- Create: `applications/services/poison_tools/adapters/poison_tool_gpio.c`
- Create: `dashboard/src/tools/families/GpioTool.tsx`
- Test: `applications/debug/unit_tests/tests/tools/poison_tool_gpio_test.c`
- Test: `dashboard/src/tools/families/GpioTool.test.tsx`
- Test: `tools/tests/test_marauder.py`

**Steps:**

1. Integrate current pin inventory/read/write paths with declared pin mode, pull, level, sampling rate, duration, and bounded result stream.
2. Reject reserved pins, voltage/mode conflicts, simultaneous incompatible ownership, unbounded sampling, and output changes without exact confirmation.
3. Test safe input, approved output, conflict, cancel, disconnect fail-safe state, overrun reporting, and evidence export.

### Task 15: Integrate USB and HID workflows

**Current implementation status (2026-08-21):** The shared USB/HID adapter now switches to the
real Flipper USB HID interface, exposes connection state plus keyboard/mouse operations, releases
all held inputs, and restores the prior USB interface on teardown. It is exported through the Poison
tool service ABI; host detach and physical HID evidence remain hardware verification requirements.

**Files:**
- Modify: `applications/main/bad_usb/application.fam`
- Modify: `applications/system/hid_app/application.fam`
- Create: `applications/services/poison_tools/adapters/poison_tool_usb_hid.c`
- Create: `dashboard/src/tools/families/UsbHidTool.tsx`
- Test: `applications/debug/unit_tests/tests/tools/poison_tool_usb_hid_test.c`
- Test: `dashboard/src/tools/families/UsbHidTool.test.tsx`

**Steps:**

1. Integrate inspection and execution of signed local payloads with declared layout/interface, visible device state, progress, pause/stop, and bounded logs.
2. Require unlocked-device policy and exact confirmation for execution; reject hidden/autostart execution, undeclared USB identities/interfaces, and payload mutation after approval.
3. Test detach, host rejection, cancellation, lock transition, script error, descriptor conflict with control transport, and recovery to the prior USB mode.

### Task 16: Integrate BLE and Bluetooth HID workflows

**Current implementation status (2026-08-21):** The shared BLE adapter now starts the existing
Bluetooth HID profile, uses the Flipper Bluetooth HAL for active-state inspection and advertising,
exposes keyboard and mouse reports through the real HID profile, releases all held reports, and
restores the default profile during teardown. Physical pairing/input evidence remains a hardware
verification requirement.

**Files:**
- Modify: `applications/services/bt/application.fam`
- Modify: `applications/system/hid_app/application.fam`
- Create: `applications/services/poison_tools/adapters/poison_tool_ble.c`
- Create: `dashboard/src/tools/families/BleTool.tsx`
- Test: `applications/debug/unit_tests/tests/tools/poison_tool_ble_test.c`
- Test: `dashboard/src/tools/families/BleTool.test.tsx`

**Steps:**

1. Expose bounded target-pairing/status and approved HID controls while keeping the M1 PoisonedOS control-session identity distinct from target HID pairing.
2. Gate target pairing, identity reset, advertising changes, and HID execution separately; preserve the active control channel or report an explicit transition requirement.
3. Test identity confusion, wrong peer, reconnect, cancel, radio contention, control-session revocation, and target-pairing deletion confirmation.

### Task 17: Integrate serial and expansion workflows

**Files:**
- Modify: `applications/services/expansion/application.fam`
- Modify: `applications/main/gpio/application.fam`
- Create: `applications/services/poison_tools/adapters/poison_tool_serial.c`
- Create: `dashboard/src/tools/families/SerialTool.tsx`
- Test: `applications/debug/unit_tests/tests/tools/poison_tool_serial_test.c`
- Test: `dashboard/src/tools/families/SerialTool.test.tsx`

**Steps:**

1. Configure only supported UART parameters, arbitrate pins with GPIO/expansion, stream sequence-numbered bounded RX/TX logs, and save selected bytes as evidence.
2. Gate transmit independently from observe; reject unsupported baud/frame settings, reserved pins, unbounded buffers, and output after stop/disconnect.
3. Test overrun/truncation markers, binary data, cancellation, cable loss, pin contention, denied transmit, and reconnect without duplicate writes.
4. Provision curated ESP coprocessor firmware only from an authoritative pinned manifest:
   identify the existing hardware name when possible, detect chip family and flash size,
   reject ambiguous board profiles, verify every input segment, use declared offsets, verify
   flash readback, and skip a repeated write when the exact curated version is already running.

### Task 18: Integrate storage and archive workflows

**Files:**
- Modify: `applications/services/storage/application.fam`
- Modify: `applications/main/archive/application.fam`
- Create: `applications/services/poison_tools/adapters/poison_tool_storage.c`
- Create: `dashboard/src/tools/families/StorageTool.tsx`
- Test: `applications/debug/unit_tests/tests/tools/poison_tool_storage_test.c`
- Test: `dashboard/src/tools/families/StorageTool.test.tsx`

**Steps:**

1. Integrate media/status inspection, bounded file hashing, selected artifact export, and archive operations through M2 VFS/evidence services.
2. Route every mutation through M2 journaling and M1 confirmation; never call a physical storage backend directly from the browser adapter.
3. Test media removal, traversal, archive bomb/duplicate path, hash mismatch, full media, cancel, denied delete, transaction recovery, and verified export.

**Achievement check:** All ten families have independent unit, policy, dashboard, device-only, and physical-device evidence; no catalog entry advertises a command that its adapter does not implement.

## Achievement A3.6: First-Class Student and Educator Workflows

### Task 19: Implement lesson execution, progress, and resettable workspaces

**Files:**
- Create: `data/poison/lessons/lesson.schema.json`
- Create: `data/poison/lessons/getting-started.json`
- Create: `applications/services/poison_lessons/application.fam`
- Create: `applications/services/poison_lessons/poison_lessons.c`
- Create: `applications/services/poison_lessons/poison_lessons.h`
- Create: `applications/services/rpc/rpc_poison_lessons.c`
- Create: `dashboard/src/education/LessonRunner.tsx`
- Create: `dashboard/src/education/ClassroomManager.tsx`
- Test: `applications/debug/unit_tests/tests/education/poison_lessons_test.c`
- Test: `dashboard/src/education/LessonRunner.test.tsx`

**Steps:**

1. Define signed lesson packs and steps for explanation, device action, browser action, observation, evidence checkpoint, knowledge check, starter code/dataset, permitted tools, and reset.
2. Create a complete introductory lesson using only capabilities verified in M1–M3.
3. Snapshot each student workspace before a lab; reset only that workspace and never unrelated cases or evidence.
4. Implement versioned per-student progress, checkpoint attempts, locally queued synchronization, explicit completion rules, and exportable progress records that reference evidence by digest without copying protected contents.
5. Support full lesson completion from the device and an enhanced synchronized instructor/student browser view.
6. Test resume, offline use, version migration, instructor policy, student isolation, restricted tools/frequencies/buses/native code/destructive actions, reset, accessible keyboard navigation, and readable error recovery.

### Task 20: Implement educator authoring, assignment, collection, and review

**Files:**
- Create: `dashboard/src/education/LessonAuthor.tsx`
- Create: `dashboard/src/education/AssignmentManager.tsx`
- Create: `dashboard/src/education/ProgressReview.tsx`
- Create: `applications/services/poison_lessons/poison_assignments.c`
- Create: `applications/services/poison_lessons/poison_assignments.h`
- Test: `dashboard/src/education/LessonAuthor.test.tsx`
- Test: `applications/debug/unit_tests/tests/education/poison_assignments_test.c`

**Steps:**

1. Test create/edit/preview/version/sign/import/export, assign/unassign, roster aliases, due-state, progress collection, feedback export, and conflict resolution while offline.
2. Validate every authored step against installed capabilities, tool catalog, policy restrictions, starter assets, reset behavior, and accessibility requirements before signing or assignment.
3. Keep identity/roster mapping local by default; use pseudonymous learner IDs in exports unless the educator explicitly includes names.
4. Require assignment imports and progress merges to be signed, schema-valid, idempotent, and scoped to the correct course/device/workspace.
5. Run dashboard, lesson, package, policy, and local-only tests → Expected: an educator can author and distribute a lesson and collect verifiable progress without a hosted service.

## Achievement A3.7: Destructive-Action Coverage and Device-Only Recovery

### Task 21: Integrate the M1 exact-target confirmation service across M2–M3

**Files:**
- Modify after M1 Task 8 creates it: `applications/services/rpc/poison_confirmation.c`
- Modify after M1 Task 8 creates it: `applications/services/rpc/rpc_poison_confirmation.c`
- Modify after M1 Task 8 creates it: `dashboard/src/security/ConfirmationDialog.tsx`
- Extend existing M1 coverage: `applications/debug/unit_tests/tests/rpc/poison_confirmation_test.c`
- Extend existing M1 coverage: `dashboard/src/security/ConfirmationDialog.test.tsx`

**Steps:**

1. Write table-driven tests for destructive storage, firmware, radio-policy, identity, native-code, application, profile, and workspace operations.
2. Inventory every M2–M3 destructive caller and map it to the M1 confirmation contract; the inventory must cover storage, evidence, migration, package, update, radio policy, identity, native code, application, profile, lesson/workspace, and recovery operations.
3. Add caller-specific regressions for generic confirmation, changed arguments, reuse, timeout, disconnect, different-client approval, and mutation between preview and execution.
4. Route every inventoried caller through the existing service and fail the coverage test if a destructive command is added without a confirmation-policy row.
5. Run firmware policy/RPC and dashboard tests → Expected: no destructive action executes without its exact approved target and consequence.

### Task 22: Implement dashboard-independent recovery mode

**Files:**
- Create: `applications/system/poison_recovery/application.fam`
- Create: `applications/system/poison_recovery/poison_recovery.c`
- Create: `applications/system/poison_recovery/poison_recovery_boot.c`
- Modify: `applications/system/updater/updater.c`
- Modify: `applications/system/updater/application.fam`
- Create: `applications/system/poison_recovery/scenes/recovery_menu.c`
- Create: `applications/system/poison_recovery/scenes/firmware_restore.c`
- Create: `applications/system/poison_recovery/scenes/profile_restore.c`
- Create: `applications/system/poison_recovery/scenes/storage_reindex.c`
- Test: `applications/debug/unit_tests/tests/recovery/poison_recovery_test.c`
- Create: `tools/hil/suites/device_recovery.py`

**Steps:**

1. Test entry gesture, locked-device policy, verified last-known-good firmware discovery from Task 4, profile restore, VFS/evidence index rebuild, cancellation, corrupt media, and power interruption.
2. Integrate a boot-time gesture and failed-health fallback before the desktop starts, then implement recovery entirely with device buttons/display and signed local artifacts; no dashboard, bridge, account, or network may be required.
3. Preserve cases, evidence, source projects, and audit history during firmware/profile/index recovery.
4. Run `python3 tools/hil/run_suite.py --suite device-recovery` → Expected: a device with corrupt active firmware metadata, settings, or storage index returns to a valid state without deleting user evidence.

## Achievement A3.8: Complete Field and Classroom E2E

### Task 23: Prove update, package, profile, every tool, lesson, and recovery workflow on hardware

**Files:**
- Create: `dashboard/e2e/apps-tools-education.spec.ts`
- Create: `tools/hil/suites/apps_tools_education.py`
- Create: `docs/testing/m3-field-classroom-protocol.md`

**Steps:**

1. Install a signed application, lesson, tool-data pack, and UI pack; reject a tampered package, update each type, force activation failure, and verify rollback on a physical device.
2. Stage and activate a signed firmware/resources candidate, interrupt activation, prove boot rollback, then complete a healthy update and verify its audit trail.
3. Run one structured app and one legacy app from the browser, save real output as evidence, and verify it in the case workspace.
4. Run the required allowed/denied/cancel/reconnect/evidence scenario for each of the ten tool families on its actual hardware path.
5. Apply/export/reset/import a field profile and classroom profile.
6. Complete the introductory lesson device-only, then author/sign/import/assign/complete/export it with an instructor browser and constrained student client.
7. Exercise exact-target confirmation across destructive operation classes, enter device-only recovery, restore firmware/profile/index, and verify case/evidence digests.
8. Run `pnpm --dir dashboard e2e --grep @physical-apps-education`, `python3 tools/hil/run_suite.py --suite apps-tools-education`, `python3 tools/hil/run_suite.py --suite tool-families`, and `python3 tools/hil/run_suite.py --suite device-recovery` → Expected: all workflows pass without mock components.

## M3 Exit Gate

```bash
./fbt lint_all
./fbt f7
./fbt FIRMWARE_APP_SET=unit_tests
python3 tools/hil/run_suite.py --suite firmware-units
python3 tools/catalog/validate_catalog.py
pnpm --dir dashboard verify
python3 tools/rust/cargo.py test --workspace --manifest-path bridge/Cargo.toml
python3 tools/hil/run_suite.py --suite apps-tools-education
python3 tools/hil/run_suite.py --suite tool-families
python3 tools/hil/run_suite.py --suite device-recovery
```

Expected: all exit `0`; FR-23–FR-32 and FR-53–FR-55 pass, every content class and firmware update has verified rollback, all ten professional tool families pass independently, and professional plus classroom workflows succeed on real hardware. Show diffs and request approval before commits.

## Deferred Findings

Record out-of-scope findings here with evidence, impact, and owner. Do not implement them during M3.
