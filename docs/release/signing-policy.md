# PoisonedOS Release Signing Policy

- **Status:** Approved design; production signing infrastructure is not commissioned
- **Decision:** ADR-0004
- **Algorithm:** ECDSA over NIST P-256 with SHA-256, DER-encoded signature
- **Manifest encoding:** UTF-8 JSON, sorted keys, no insignificant whitespace, with the top-level `signature` field omitted from signed bytes

This policy covers firmware, packages, content, provenance, and support-bundle integrity records. M0 implements test-only verification in `tools/signing/verify_manifest.py`; it does not create a production key, certificate, signer service, or release authorization.

## Authority Hierarchy

The production root is offline, non-exportable, and requires two of three assigned custodians to authorize an operation. It signs only scoped intermediate authority records and emergency revocation statements. It never signs routine artifacts.

Online intermediates are separated by environment and scope. Compromise of one intermediate must not authorize another scope or production environment.

| Scope | May sign | Operational owner | Maximum validity |
|---|---|---|---:|
| `firmware` | Firmware/update manifests for an exact hardware target and channel | Release, with Security approval | 90 days |
| `package` | Native FAP, JavaScript, and Wasm package admission manifests | Package Review and Security | 90 days |
| `content` | Tool-data, lesson, theme, icon/font, and profile packs | Product/Education with Security policy | 180 days |
| `provenance` | Reproducible-build and source/material attestations | Release/Toolchain | 90 days |
| `support-bundle` | Redaction/integrity records for user-approved diagnostic exports | Support and Security | 90 days |

Development, HIL, staging, and production use distinct roots and intermediates. A non-production key ID, chain, or artifact is never accepted by a production trust store.

## Required Signed Claims

Every signed artifact manifest binds at least its schema/version, scope, key ID, artifact SHA-256, artifact type, target hardware/ABI/API where applicable, requested capabilities, channel, monotonically increasing release sequence, issuance and expiry, and provenance reference. A verifier rejects an unknown required semantic rather than guessing.

The revocation-manifest contract is `schemas/poison/revocation-manifest.schema.json`. Revocation statements bind scope, issuer key ID, monotonically increasing sequence, validity window, revoked key IDs, revoked artifact digests, and `ECDSA-P256-SHA256` before the signature is checked.

## Verification Order

1. Parse with strict size/depth bounds and reject unknown top-level fields for the active schema.
2. Validate schema, algorithm, types, identifiers, digests, target, API/ABI, capabilities, scope, and channel.
3. Resolve the pinned trust anchor and confirm that the public key and authority record allow the requested environment and scope.
4. Canonicalize the manifest and verify its P-256/SHA-256 signature.
5. Enforce issuance/expiry against trusted time where available and reject a validity interval that moves backward.
6. Enforce a scope-specific sequence greater than or equal to the last accepted floor; persist the new floor before activation.
7. Apply the newest valid revocation statement and reject a revoked signer or artifact digest.
8. Verify artifact bytes, exact target/API/ABI, policy/capability intersection, and available rollback space.
9. Stage transactionally, record the last known-good object, activate, health-check, and roll back on failure.

A device without trustworthy current time still enforces signature, scope, sequence, target, and revocation. It may use the last trusted signed time floor but must not move time backward or accept an artifact solely because the RTC is unset.

## Rotation and Expiry

- Root authority records have a maximum five-year lifetime and require an annual offline integrity/restore exercise.
- Online intermediates rotate at least every 90 days, or 30 days before expiry, whichever comes first.
- Content intermediates may last up to 180 days because they cannot authorize firmware or native package execution.
- Routine rotation overlaps old and new intermediates only long enough for staged rollout and rollback verification.
- An intermediate stops signing before expiry; expired keys remain available only for historical verification.
- Trust stores accept a new root only through a separately approved root-transition ceremony with dual signatures during the transition window.

## Revocation and Emergency Authority

Revocation sequence numbers are monotonic per scope. Distribution endpoints, the bridge, and the device cache the highest accepted sequence and reject lower sequences. Catalog removal alone is not revocation.

Security may request emergency revocation; Release confirms affected digests/channels and executes distribution. If an online revocation authority is unavailable or suspected compromised, the offline root issues an emergency statement. Revocation quarantines affected packages and blocks future execution or activation but does not delete evidence or user source.

Critical signature, key, or package-verification compromise halts publication immediately. The incident record must identify affected key IDs, digests, scopes, first/last known use, replacement authority, device/cache propagation evidence, and recovery instructions.

## Rollback Protection

Every activation domain persists its highest accepted release and revocation sequence in protected transactional state. Recovery may activate the recorded last known-good object even when its release sequence is lower only through the dedicated recovery policy, physical confirmation, and an audit record; it must not lower the normal admission floor.

## Development Keys and Repository Rules

- Tests generate ephemeral P-256 keys inside temporary directories and destroy them with the test workspace.
- Repository fixtures may contain public keys and invalid/example signatures, never a production or reusable private key.
- Private-key suffixes and signing-key directories are ignored, but ignore rules are not a security boundary; secret scanning and review remain mandatory.
- Production signing requires the completed key ceremony, commissioned protected hardware/service, access logging, dual-control roles, and a tested revocation distribution path.

PoisonedOS must not be distributed even with a valid signature until the recorded flipperzero-protobuf license grant and mJS GPL compatibility blockers are resolved.
