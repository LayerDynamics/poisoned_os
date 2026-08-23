# ADR-0011: One Verified Content Update Transaction

- **Status:** Accepted
- **Decision owner:** Platform, Security, and Release
- **Accepted:** 2026-08-22
- **Applies to:** Firmware, applications, lessons, tool data, themes, font/icon packs, menus, resources, and dashboard UI packs

## Context

PoisonedOS is a professional field and classroom platform that must update firmware and managed content without leaving an unverified candidate active or destroying the last verified version. Flipper firmware updates already use an `update.fuf` compatibility manifest, a staged loader, firmware/radio/resource artifacts, an internal backup, a `.fupdate` pointer, and boot-time activation. Managed applications and content need the same verification, confirmation, health, and rollback vocabulary instead of separate lifecycle implementations.

CRC protects the existing loader transfer against accidental corruption, but it does not establish signing authority, release sequence, revocation state, or artifact origin. ADR-0004 requires ECDSA P-256 with SHA-256, DER signatures, scoped trust anchors, and monotonic rollback protection.

## Decision

PoisonedOS uses one transaction state machine for every managed update class:

`Discovered → Receiving → Staged → Verified → AwaitingConfirmation → Activating → Healthy`

An operation may instead terminate as `RolledBack` or `Quarantined`. A restarted transaction from `Receiving` through `Activating` recovers the previous verified object when its rollback artifact is valid. Missing or invalid rollback material quarantines the candidate and never marks it healthy.

### Signed Envelope

The existing `update.fuf` remains the compatibility carrier for the staged loader and firmware/radio/resource paths. A PoisonedOS update bundle additionally contains:

- a bounded canonical UTF-8 JSON manifest;
- a detached DER-encoded ECDSA P-256/SHA-256 signature over the exact canonical manifest bytes; and
- only artifact paths that are relative, normalized, non-duplicated, and contained by the update directory.

The signed manifest binds schema version, environment, scope, signing key ID, artifact type, target hardware/API/ABI, channel, release sequence, issuance and expiry, provenance reference, rollback identity, and each artifact path, byte length, and SHA-256 digest. Unknown required semantics, duplicate fields or artifacts, unsafe paths, unsupported content types, wrong targets, incompatible APIs, revoked signers or artifacts, and non-increasing release sequences fail closed.

Trust anchors are pinned by environment and authority scope. A public key carried by the candidate cannot authorize that candidate. Development, HIL, staging, and production keys are distinct; production publication remains unavailable until the ADR-0004 ceremony and signing service are commissioned.

### Verification and Atomicity

The transaction verifies in this order before activation:

1. Bound and parse the compatibility and signed manifests.
2. Resolve the pinned environment/scope trust anchor and revocation state.
3. Verify the detached signature over the exact canonical JSON bytes.
4. Enforce target, API/ABI, channel, time-floor where available, and monotonic sequence rules.
5. Verify every artifact byte length and SHA-256 digest inside the update directory.
6. Confirm sufficient staging and rollback storage.
7. Persist the candidate, previous verified identity, transaction state, and new sequence floor atomically.
8. Obtain exact-target confirmation where required, then activate.

The `.fupdate` pointer and pre-update boot mode may be armed only after the shared coordinator reaches `Activating`. A candidate cannot bypass the coordinator through the legacy firmware or package paths.

### Protected Targets and Confirmation

Firmware, staged loaders, boot resources, trust roots, protected packages, rollback operations, and recovery-floor changes require the M1 exact-target confirmation token. The confirmation binds the candidate digest and operation; a rejected or expired confirmation leaves the transaction safely retryable in `AwaitingConfirmation`.

### Boot Health and Rollback

Activation starts a bounded health window. Firmware health requires the expected origin/version identity, required startup services, readable storage, and a persisted coordinator health signal. Managed content health requires its class-specific loader or index check. Success transitions to `Healthy`; timeout, crash, explicit failure, or invalid state restores the last verified object and transitions to `RolledBack`.

Normal admission never lowers the persisted release or revocation floor. A lower last-known-good version may run only through the dedicated recovery policy with physical confirmation and an audit record. User projects, evidence, settings, and unrelated content are outside the replacement boundary and remain preserved.

### Observability

Discovery, admission result, state changes, confirmation, activation, health, rollback, cancellation, and quarantine are audited with bounded identifiers and digests. Diagnostics expose bounded counters for signature/revocation rejection, stage failure, health timeout, rollback, and recovery. They never expose manifest bodies, paths, signer material, private keys, or package contents.

## Compatibility

Official Flipper `update.fuf` parsing and staged-loader behavior remain available as the low-level transport. PoisonedOS-controlled activation additionally requires the signed envelope. Unsupported legacy unsigned bundles are inspectable but cannot be activated through the PoisonedOS managed-update path.

## Implementation Status

The shared coordinator and application-package adapter implement the common state vocabulary, recovery boundaries, rollback, health reporting, and retryable exact confirmation. Firmware includes a bounded P-256/SHA-256 DER verification primitive and its upstream known-answer regression. Binding the signed JSON envelope and pinned scoped trust store to `lib/update_util`, boot-health persistence, authenticated RPC/bridge/dashboard control, and physical interruption evidence remain in progress.

## Consequences

Every update class has the same externally visible lifecycle and rollback guarantees, and the existing Flipper updater remains the only firmware flashing implementation. Update creation now requires scoped signing inputs, and production distribution cannot silently fall back to unsigned artifacts. Additional manifest, trust-store, persistence, and health checks consume firmware and host-tooling space but prevent CRC-only or unrelated state machines from authorizing activation.
