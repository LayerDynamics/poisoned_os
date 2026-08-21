# M1 Secure Browser-to-Device Control Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use lore:execute to implement this plan task-by-task.
> **Scope guard:** Do ONLY what is listed here. If you discover adjacent issues, record them in the deferred-findings section and continue. Do NOT fix them.

**Goal:** Prove that a real phone or computer browser can securely pair with, inspect, control, and launch applications on a physical PoisonedOS device over USB and BLE.
**Architecture:** Add an authenticated RPC v2 session layer over existing protobuf RPC without removing legacy handlers. Generate shared TypeScript/Rust contracts, connect an offline PWA directly where browser APIs permit, and use a loopback-only Rust bridge elsewhere.
**Tech Stack:** C/C++, nanopb/protobuf, mbedTLS, TypeScript/Vite/PWA, Rust/Tokio, USB/BLE, Vitest, Playwright, physical-device E2E.
**Practices:** Contract-first, typed-first, security TDD, real-hardware E2E, approval-gated commits.
**Required skills:** `lore:test-driven-development`, `lore:security`, `lore:e2e-test-expert`, `lore:verification-before-completion`

---

## Achievement Ledger

| Achievement | Status before implementation | Required closing evidence |
|---|---|---|
| A1.0 Product and transport decisions | Not started | ADR-0001/0002 and the machine-checked feature/client matrices are approved. |
| A1.1 Versioned session contract | Not started | Compatibility, crypto, replay, sequencing, cancellation, and flow-control tests pass on USB and BLE. |
| A1.2 Role policy and device UX | Not started | Complete role/capability matrix and physical pairing/revocation workflows pass. |
| A1.3 Audit, confirmation, and diagnostics | Not started | Chain, redaction, exact-target mutation/replay, and support-export tests pass. |
| A1.4 Dashboard and bridge foundations | Not started | Offline PWA and loopback-origin isolation pass on every supported client row. |
| A1.5 Complete remote operation | Not started | Real browser/transport/firmware app, screen, input, stream, and reconnect E2E passes. |

**Canonical task commands:** firmware tasks run `./fbt FIRMWARE_APP_SET=unit_tests` and `python3 tools/hil/run_suite.py --suite firmware-units`; dashboard tasks run `pnpm --dir dashboard verify`; bridge tasks run `cargo test --workspace --manifest-path bridge/Cargo.toml` and `cargo clippy --manifest-path bridge/Cargo.toml -- -D warnings`; physical workflows run the exact `tools/hil/run_suite.py` and Playwright commands named by the task. Record command, versions, physical IDs where applicable, exit code, and evidence digest before changing a ledger row.

## Achievement A1.0: Product and Transport Decisions

### Task 1: Resolve distribution and hosted-service boundaries

**Files:**
- Create: `docs/decisions/ADR-0001-distribution-and-hosted-boundary.md`
- Create: `schemas/poison/feature-matrix.schema.json`
- Create: `config/features/local-only.json`
- Test: `tools/tests/test_feature_matrix.py`

**Steps:**

1. Encode local-only, self-hosted, and hosted-capable builds without allowing device control, files, evidence, customization, or installed workloads to depend on an account or WAN.
2. Decide which hosted components are V1-shipped, disabled, or post-V1, including catalog/build/sync/organization boundaries and source/license obligations.
3. Write tests that fail if a core capability imports a hosted-only client, requires an account flag, or makes an external request in local-only configuration.
4. Run `python3 -m unittest tools.tests.test_feature_matrix` → Expected: every feature has an owner, build flag, local behavior, and data boundary.

### Task 2: Resolve supported direct and bridge transports

**Files:**
- Create: `docs/decisions/ADR-0002-supported-client-transports.md`
- Create: `config/supported-clients.json`
- Create: `tools/verify_supported_clients.py`
- Test: `tools/tests/test_supported_clients.py`

**Steps:**

1. Enumerate the exact V1 phone/computer OS, browser, version floor, direct WebSerial/WebUSB/Web Bluetooth availability, bridge requirement, installation method, and unsupported combinations.
2. Tie every supported row to an available physical test host and device transport; a row without hardware evidence cannot be marked supported.
3. Define one transport-independent session behavior and explicit adapter limitations, including BLE MTU/credit behavior and USB RPC ownership.
4. Run `python3 tools/verify_supported_clients.py --config config/supported-clients.json` → Expected: no untested supported row and no ambiguous direct/bridge selection.

## Achievement A1.1: Versioned Session Contract

### Task 3: Define RPC v2 schemas and generated bindings

**Files:**
- Create: `assets/protobuf/poison_session.proto`
- Create: `assets/protobuf/poison_session.options`
- Modify: `assets/protobuf/flipper.proto:1-138`
- Create: `schemas/poison/session.schema.json`
- Generate: `dashboard/src/generated/poison-session.ts` through the M0 protocol generator
- Generate: `bridge/src/generated/poison_session.rs` through the M0 protocol generator
- Test: `applications/debug/unit_tests/tests/rpc/poison_session_test.c`

**Contract:**

```protobuf
message SessionEnvelope {
  uint32 protocol_version = 1;
  fixed64 session_id = 2;
  fixed64 sequence = 3;
  fixed64 acknowledgement = 4;
  string channel = 5;
  bytes payload = 6;
  bytes authentication_tag = 7;
}
```

**Steps:**

1. Add failing encode/decode, unknown-version, oversize, and sequence-wrap tests.
2. Build and run `./fbt FIRMWARE_APP_SET=unit_tests` followed by `python3 tools/hil/run_suite.py --suite firmware-units` → Expected: new contract tests FAIL before handlers/bindings exist.
3. Add pairing negotiation, session envelope, channel negotiation, stable error, cancellation, credit-window, and resume-token messages with explicit nanopb bounds; generate all bindings through the M0 generator rather than committing hand-written generated types.
4. Run firmware, dashboard typecheck, and bridge tests → Expected: PASS with identical field/tag snapshots.
5. Stage diffs and request approval for proposed `feat(protocol): define authenticated session envelope`.

### Task 4: Implement common command, stream, and error semantics

**Files:**
- Create: `assets/protobuf/poison_common.proto`
- Create: `assets/protobuf/poison_common.options`
- Create: `applications/services/rpc/rpc_poison_channel.c`
- Create: `applications/services/rpc/rpc_poison_channel.h`
- Test: `applications/debug/unit_tests/tests/rpc/poison_channel_test.c`
- Test: `dashboard/src/session/ChannelState.test.ts`
- Test: `bridge/tests/channel_flow.rs`

**Steps:**

1. Test unique command IDs, idempotent replay, explicitly non-repeatable commands, cancellation acknowledgement, stable error code/message/retryability/correlation ID, and unknown enum/optional fields.
2. Test channel-local sequence/acknowledgement, bounded credit windows, retransmission, duplicate suppression, gap reporting, truncation, reconnect resumption, and resume refusal after expiry/revocation.
3. Implement the bounded firmware channel table over the existing 1,024-byte RPC stream buffer (`applications/services/rpc/rpc.h:12`) without unbounded per-channel allocation.
4. Run firmware, dashboard, and bridge focused tests → Expected: identical fixtures and no accepted unexplained stream gap.

### Task 5: Implement pairing, encryption, counters, and revocation

**Files:**
- Create: `applications/services/rpc/rpc_poison_session.c`
- Create: `applications/services/rpc/rpc_poison_session.h`
- Create: `applications/services/rpc/rpc_poison_crypto.c`
- Create: `applications/services/rpc/rpc_poison_crypto.h`
- Modify: `targets/f7/furi_hal/furi_hal_crypto.c:538-747`
- Modify: `applications/services/rpc/rpc.c:37-87,401-404`
- Modify: `applications/services/rpc/rpc_i.h:14-44`
- Test: `applications/debug/unit_tests/tests/rpc/poison_crypto_test.c`
- Test: `applications/debug/unit_tests/tests/rpc/poison_replay_test.c`

**Steps:**

1. Write known-answer tests for ephemeral P-256 ECDH, HKDF-SHA-256, AES-256-GCM, transcript binding, role/capability negotiation binding, and tamper rejection.
2. Write replay, out-of-order, expired-session, revoked-client, and counter-exhaustion tests.
3. Define plaintext discovery/pairing frames separately from encrypted application frames; derive nonces from session direction plus monotonic counter and never transmit/reuse a session key.
4. Use the existing device-unique enclave slot `FURI_HAL_CRYPTO_ENCLAVE_UNIQUE_KEY_SLOT` (`targets/furi_hal_include/furi_hal_crypto.h:56-61`) as the hardware-rooted wrapping facility and store only versioned enclave-wrapped pairing records; do not allocate an undocumented user slot. Verify unavailable, provisioning-failure, corrupt-record, and key-loss behavior against `targets/f7/furi_hal/furi_hal_crypto.c:88-194`.
5. Implement explicit session state transitions: `Unpaired → Negotiating → Confirming → Active → Closing/Revoked`.
6. Run the physical firmware-unit and packet-capture suites → Expected: all valid vectors pass; every invalid transition is rejected and audited; no application payload or reusable secret is visible.

**Achievement check:** A packet capture reveals no plaintext payload or reusable session secret; replayed frames never reach an RPC handler.

## Achievement A1.2: Role Policy and Device UX

### Task 6: Implement paired-client and role policy storage

**Files:**
- Create: `applications/services/rpc/poison_pairing_store.c`
- Create: `applications/services/rpc/poison_pairing_store.h`
- Create: `applications/services/rpc/poison_policy.c`
- Create: `applications/services/rpc/poison_policy.h`
- Create: `assets/protobuf/poison_policy.proto`
- Create: `assets/protobuf/poison_policy.options`
- Test: `applications/debug/unit_tests/tests/rpc/poison_policy_test.c`

**Steps:**

1. Write table-driven tests for Owner, Operator, Instructor, Student, and Observer capabilities.
2. Test atomic pairing persistence, revoke-all-sessions, corrupt-record recovery, and maximum-client behavior.
3. Implement least-privilege intersection across role, requested capability, lock state, and physical confirmation.
4. Run `./fbt FIRMWARE_APP_SET=unit_tests` and `python3 tools/hil/run_suite.py --suite firmware-units` → Expected: PASS; no default role grants destructive or native-code capability.

### Task 7: Add physical pairing and revocation screens

**Files:**
- Create: `applications/settings/poison_security_settings/application.fam`
- Create: `applications/settings/poison_security_settings/poison_security_settings.c`
- Create: `applications/settings/poison_security_settings/scenes/pair_confirm.c`
- Create: `applications/settings/poison_security_settings/scenes/paired_clients.c`
- Create: `applications/settings/poison_security_settings/scenes/revoke_confirm.c`
- Test: `applications/debug/unit_tests/tests/rpc/poison_pairing_ui_test.c`

**Steps:**

1. Test exact confirmation code, fingerprint, client name, requested role, timeout, cancel, and revoke flows.
2. Implement device-side confirmation; dashboard-only approval is insufficient.
3. Run headless scene tests and physical input test → Expected: canceled/expired pairing creates no stored client.

**Achievement check:** Owner can identify and revoke a client entirely from the Flipper.

## Achievement A1.3: Shared Audit, Confirmation, and Diagnostics Foundations

### Task 8: Implement auditable decisions and exact-target confirmation

**Files:**
- Create: `assets/protobuf/poison_audit.proto`
- Create: `assets/protobuf/poison_audit.options`
- Create: `assets/protobuf/poison_confirmation.proto`
- Create: `assets/protobuf/poison_confirmation.options`
- Create: `applications/services/poison_audit/application.fam`
- Create: `applications/services/poison_audit/poison_audit.c`
- Create: `applications/services/poison_audit/poison_audit.h`
- Create: `applications/services/rpc/poison_confirmation.c`
- Create: `applications/services/rpc/poison_confirmation.h`
- Create: `applications/services/rpc/rpc_poison_audit.c`
- Create: `applications/services/rpc/rpc_poison_confirmation.c`
- Create: `dashboard/src/audit/AuditClient.ts`
- Create: `dashboard/src/security/ConfirmationDialog.tsx`
- Test: `applications/debug/unit_tests/tests/rpc/poison_audit_test.c`
- Test: `applications/debug/unit_tests/tests/rpc/poison_confirmation_test.c`
- Test: `dashboard/src/security/ConfirmationDialog.test.tsx`

**Steps:**

1. Define bounded audit events with event ID, prior digest, actor, action, resource, decision, timestamp source, correlation ID, and safe metadata; prohibit secrets and raw evidence.
2. Define a short-lived confirmation token bound to session, role, command digest, exact target list, displayed consequence, policy version, and required physical approval.
3. Test changed arguments/target/consequence, generic approval, replay, timeout, disconnect, different client, role change, lock change, and policy revocation.
4. Integrate pairing, authentication, role changes, confirmation, revocation, app lifecycle, and policy denial callers in M1; later milestones must integrate their own caller families before exit.
5. Run audit-chain, policy, RPC, and dashboard tests → Expected: every sensitive decision has one ordered redacted event and confirmation cannot authorize a changed command.

### Task 9: Create bounded diagnostic events before feature expansion

**Files:**
- Create: `assets/protobuf/poison_diagnostics.proto`
- Create: `assets/protobuf/poison_diagnostics.options`
- Create: `applications/services/poison_diagnostics/application.fam`
- Create: `applications/services/poison_diagnostics/poison_diagnostics.c`
- Create: `applications/services/poison_diagnostics/poison_diagnostics.h`
- Create: `bridge/src/diagnostics.rs`
- Create: `dashboard/src/support/SupportBundle.tsx`
- Create: `schemas/poison/support-bundle.schema.json`
- Test: `applications/debug/unit_tests/tests/diagnostics/poison_diagnostics_test.c`
- Test: `bridge/tests/diagnostics.rs`
- Test: `dashboard/src/support/SupportBundle.test.tsx`

**Steps:**

1. Implement bounded counters and redacted events for session establishment, transport latency/errors, dropped/retried frames, command failures, app crashes, and policy denials.
2. Test secret/key/pairing-code/client-token redaction, malicious metadata, ring-buffer rollover, crash-path emission, counter saturation, and local-only behavior.
3. Attach correlation IDs from the common command contract and expose diagnostics only to authorized roles.
4. Implement a local support-bundle snapshot with schema version, component versions, bounded counters, redacted errors, user-selected optional records, consent record, file digests, and preview-before-export; exclude secrets, keys, pairing codes, client tokens, payload contents, source files, evidence bytes, and stable private identifiers by default.
5. Run firmware, bridge, and dashboard tests → Expected: actionable aggregate health and a verifiable local support export without prohibited content.

## Achievement A1.4: Dashboard and Bridge Foundations

### Task 10: Create the offline dashboard shell

**Files:**
- Create: `dashboard/package.json`
- Create: `dashboard/tsconfig.json`
- Create: `dashboard/vite.config.ts`
- Create: `dashboard/src/main.tsx`
- Create: `dashboard/src/app/App.tsx`
- Create: `dashboard/src/session/SessionClient.ts`
- Create: `dashboard/src/transports/Transport.ts`
- Create: `dashboard/src/transports/WebSerialTransport.ts`
- Create: `dashboard/src/transports/WebUsbTransport.ts`
- Create: `dashboard/src/transports/WebBluetoothTransport.ts`
- Create: `dashboard/src/sw.ts`
- Test: `dashboard/src/session/SessionClient.test.ts`

**Steps:**

1. Define a typed `Transport` interface with discover/connect/read/write/close, MTU, backpressure, health, and abort semantics.
2. Write session tests for handshake, counters, reconnect, cancellation, and transport loss.
3. Implement installable offline PWA shell and secure session client.
4. Run `pnpm --dir dashboard install`, `pnpm --dir dashboard test`, `pnpm --dir dashboard typecheck`, and `pnpm --dir dashboard build` → Expected: PASS and offline assets emitted.

### Task 11: Create the loopback-only Rust bridge

**Files:**
- Create: `bridge/Cargo.toml`
- Create: `bridge/src/main.rs`
- Create: `bridge/src/api.rs`
- Create: `bridge/src/auth.rs`
- Create: `bridge/src/device.rs`
- Create: `bridge/src/transports/usb.rs`
- Create: `bridge/src/transports/ble.rs`
- Test: `bridge/tests/origin_auth.rs`
- Test: `bridge/tests/session_forwarding.rs`
- Test: `bridge/tests/api_contract.rs`

**Steps:**

1. Write failing tests for non-loopback bind, missing origin token, wrong Origin, cross-session access, and connection cleanup.
2. Implement the exact SPEC bridge endpoints `GET /v1/devices`, `POST /v1/devices/{id}/sessions`, and `GET /v1/sessions/{id}/stream`, plus USB/BLE adapter traits and strict request/body limits.
3. Store client keys through OS secure storage abstraction; never plaintext files.
4. Run `cargo test --workspace --manifest-path bridge/Cargo.toml` and `cargo clippy --manifest-path bridge/Cargo.toml -- -D warnings` → Expected: PASS.

**Achievement check:** An unrelated browser origin cannot enumerate or open devices through the bridge.

## Achievement A1.5: Complete Remote Operation

### Task 12: Implement device status, app lifecycle, screen, and input clients

**Files:**
- Create: `dashboard/src/device/DeviceStatus.tsx`
- Create: `dashboard/src/device/RemoteScreen.tsx`
- Create: `dashboard/src/device/InputController.ts`
- Create: `dashboard/src/apps/AppLauncher.tsx`
- Create: `dashboard/src/apps/AppEvents.ts`
- Create: `dashboard/src/apps/StructuredAppClient.ts`
- Create: `dashboard/src/apps/AppConsole.tsx`
- Create: `dashboard/src/apps/AppArtifactList.tsx`
- Create: `dashboard/src/audit/AuditTimeline.tsx`
- Modify: `applications/services/rpc/rpc_app.c:441-478`
- Modify: `applications/services/rpc/rpc_gui.c:397-439`
- Create: `applications/services/rpc/rpc_poison_app_events.c`
- Create: `applications/main/poison_safe_sample/application.fam`
- Create: `applications/main/poison_safe_sample/poison_safe_sample.c`
- Create: `applications/main/poison_safe_sample/poison_safe_sample.h`
- Test: `applications/debug/unit_tests/tests/apps/poison_safe_sample_test.c`
- Test: `dashboard/src/device/RemoteScreen.test.tsx`
- Test: `dashboard/src/apps/AppLauncher.test.tsx`
- Test: `dashboard/src/apps/StructuredAppClient.test.ts`

**Steps:**

1. Test connection, firmware/API version, battery, internal/SD storage, active application, and transport-health state plus orientation, stale-frame rejection, exact press/release/short/long/repeat ordering, held-key cleanup, app lock errors, disconnect state, event gaps, log truncation, and artifact checksum failure.
2. Wrap existing RPC handlers in authenticated channel authorization without removing legacy compatibility.
3. Implement status, framebuffer rendering, complete input set, start/stop, errors, and ordered app events.
4. Implement `poison_safe_sample` as a device-launchable, non-destructive integrated application with typed status, one bounded parameter, cancellable progress, log, warning, structured result, and deterministic generated text artifact; use it as the first complete firmware-to-dashboard structured contract and the safe onboarding sample reused by M2 cases and M3 lessons.
5. Add the corresponding M1 structured client and preserve the contract as the public base that M3 extends rather than a disposable prototype.
6. Render the M1 audit event families with sequence-gap, verification, offline, and permission states; M2 adds evidence/file events.
7. Run the canonical dashboard, firmware-unit, and bridge commands → Expected: PASS with complete status fields, the real structured safe sample, and a legacy framebuffer application.

### Task 13: Add the real hardware E2E suite

**Files:**
- Create: `dashboard/e2e/pair-control.spec.ts`
- Create: `tools/hil/suites/pair_control.py`
- Create: `tools/hil/fixtures/supported-platforms.json`

**Steps:**

1. Flash the M1 candidate to the physical test device.
2. Launch the built dashboard and real bridge; no mock transport is permitted.
3. Pair by physical confirmation, verify status, stream screen, send every key type, launch/stop a real legacy app, exercise the integrated structured app and collect its logs/result/artifact, disconnect during a held key, reconnect, revoke, and verify denial.
4. Run `pnpm --dir dashboard e2e --grep @physical-pair-control` and `python3 tools/hil/run_suite.py --suite pair-control`.
5. Repeat every workflow on every supported-client row from ADR-0002, including direct and bridge routes and a WAN-blocked offline PWA launch.
6. Expected: USB and BLE meet SPEC latency targets; no stuck key, plaintext payload, unexplained event gap, external dependency, or post-revocation session remains.

## M1 Exit Gate

```bash
./fbt lint_all
./fbt f7
./fbt FIRMWARE_APP_SET=unit_tests
python3 tools/hil/run_suite.py --suite firmware-units
pnpm --dir dashboard verify
cargo test --workspace --manifest-path bridge/Cargo.toml
cargo clippy --manifest-path bridge/Cargo.toml -- -D warnings
python3 tools/hil/run_suite.py --suite pair-control
```

Expected: all exit `0`; real USB and BLE E2E evidence satisfies FR-1–FR-10, FR-51, FR-52, and FR-57, and establishes the shared confirmation/audit foundations later milestones complete for FR-53 and FR-56. Show diffs and request approval before commits.

## Deferred Findings

Record out-of-scope findings here with evidence, impact, and owner. Do not implement them during M1.
