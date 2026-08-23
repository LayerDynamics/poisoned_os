# ADR-0004: Signing Authorities

- **Status:** Accepted
- **Decision owner:** Security and Release
- **Accepted:** 2026-08-21
- **Applies to:** PoisonedOS firmware, packages, content, provenance, support-bundle integrity, and revocation

## Context

SPEC-1 requires signed, versioned, rollback-safe firmware and content while targeting an STM32WB55 firmware stack that already enables ECDSA, SHA-256, and the NIST P-256 curve. One unrestricted online signer would make a package-catalog compromise sufficient to authorize firmware, native code, lessons, profiles, and provenance. A root used for routine signing would expose the entire trust anchor to online operations.

M0 also has two unresolved distribution-license blockers. A signature proves origin and integrity; it does not grant redistribution rights or make a legally blocked release distributable.

## Decision

PoisonedOS uses ECDSA P-256 with SHA-256 and DER-encoded signatures. The production trust anchor is a non-exportable offline root requiring two-of-three custodian authorization. It signs only narrowly scoped online intermediate authority records and emergency revocations.

Development, HIL, staging, and production have separate trust roots. Production online intermediates are separated into `firmware`, `package`, `content`, `provenance`, and `support-bundle` scopes. Authority records bind environment, scope, public key, key ID, issuance/expiry, and constraints. Verifiers reject cross-scope, cross-environment, expired, revoked, wrong-target, and rollback attempts.

Security owns the offline root policy, custodian assignments, revocation decisions, and cryptographic verification requirements. Release jointly controls root ceremonies, owns firmware/provenance signing operations, and coordinates emergency distribution. Package Review owns package submissions but cannot approve its own signer authority. Product/Education owns reviewed content inputs but cannot sign firmware or native packages. Support can request a support-bundle integrity signature but never receives a firmware/package authority.

The detailed lifetime, rotation, revocation, rollback, and environment rules are normative in `docs/release/signing-policy.md`; the dual-control procedure is normative in `docs/release/key-ceremony.md`.

## Alternatives Considered

- **One online project key:** rejected because compromise crosses every artifact class and prevents least-authority revocation.
- **Routine signing with the root:** rejected because frequent access defeats the offline trust boundary.
- **RSA:** rejected because larger keys/signatures and verification costs provide no product advantage over the device-supported P-256 stack.
- **Ed25519:** not selected for V1 because the locked device cryptography configuration already supports and tests P-256 ECDSA, while an Ed25519 adoption would add a new firmware primitive and compatibility surface before M0 evidence exists.
- **Signature-only release numbers:** rejected because rollback protection and revocation freshness require persisted monotonic sequence floors.
- **Shared production/test authorities:** rejected because a test fixture or HIL compromise must never authorize production artifacts.

## Consequences

Compromise and rotation can be contained by environment and scope, and routine releases do not expose the root. Operations must commission protected signing hardware or a protected online service, assign named custodians, publish revocation data, retain verification history, and conduct ceremonies before any production signature exists. Devices and clients must store trust anchors and highest accepted sequences transactionally. More keys and procedures increase operational cost, but reduce the blast radius of any one signer.

The current repository contains only the public schema, verifier, policies, and ephemeral test-key generation. It contains no production private key and has not completed a production key ceremony.
