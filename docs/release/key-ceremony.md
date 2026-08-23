# PoisonedOS Production Key Ceremony

- **Status:** Required procedure; no production ceremony has occurred
- **Authority model:** Offline P-256 root with two-of-three custodian authorization
- **Related policy:** `docs/release/signing-policy.md`

This procedure creates or rotates production signing authority without exposing a private key to the repository, build host, meeting notes, or ordinary operator workstation.

## Roles and Separation of Duty

| Role | Responsibility | Prohibited combination |
|---|---|---|
| Ceremony Coordinator | Verifies prerequisites, reads the procedure, records public evidence | Cannot be the sole Security or Release approver |
| Security Custodians A, B, C | Hold separate authorization factors; any two authorize root operations | No custodian holds two factors |
| Release Approver | Confirms scope, environment, channel, and intermediate request | Cannot alone operate the root |
| Independent Witness | Verifies device seals, displayed public values, and transcript digest | Cannot hold a root authorization factor |
| Auditor | Reviews access logs, evidence, and reconciliation after the ceremony | Cannot alter ceremony evidence |

Named people and approved alternates must be recorded in the restricted operations register before scheduling. Repository role names are not a substitute for assigned humans.

## Preconditions

1. Security and Release approve the ceremony ticket, purpose, environment, algorithm, scopes, and expected public-key identifiers.
2. Two custodians, the Coordinator, Release Approver, and Independent Witness are physically present or use an approved independently authenticated ceremony channel.
3. The offline room/workstation and protected signing hardware are inventoried, tamper evidence is checked, firmware versions are recorded, and network interfaces are physically disabled.
4. Two independently obtained copies of the approved ceremony tooling are digest-verified against the release record.
5. Blank encrypted evidence media, printed checklists, incident contacts, and an abort plan are available.
6. No unresolved critical security finding or unexplained hardware/tool digest mismatch exists.

Any failed precondition aborts the ceremony. Operators preserve evidence and investigate; they do not improvise around a failed control.

## Root Generation

1. The Coordinator opens the ticket and records UTC time, participants, hardware serials, seals, tool digests, and purpose.
2. Two of three custodians authenticate separately to the offline protected device.
3. Generate a non-exportable ECDSA P-256 root key inside the protected boundary using its hardware random generator.
4. Export only the public key and authority metadata. Derive the key ID from the policy-defined SHA-256 public-key encoding and read it aloud twice.
5. Each custodian and the Witness compare the displayed key ID with both evidence copies before acknowledging it.
6. Create encrypted protected-device backups using the approved M-of-N recovery controls; no raw private key or single-factor backup is permitted.
7. Seal each authorization/recovery factor separately and assign it to a named custodian in a distinct approved location.

## Intermediate Issuance

1. Import a digest-verified public request containing environment, scope, key ID, public key, issuance/expiry, operator owner, and maximum sequence/channel constraints.
2. Security validates algorithm and least-authority scope; Release validates environment, channel, and operational owner.
3. Two custodians authorize the root signature.
4. Export the signed authority record and chain only. Independently verify the signature, scope, expiry, and key IDs on two clean hosts.
5. Record the public record digest and deliver it through two authenticated channels to the operational owner.
6. The online intermediate remains disabled until revocation publication, monitoring, and access logging pass their readiness tests.

## Ceremony Closeout

1. Reconcile every imported and exported file by name, byte length, and SHA-256.
2. Verify that no private-key material exists on evidence media, workstation storage, shell history, logs, printer queues, or repository paths.
3. Power down and seal the offline equipment. Record final seal identifiers and custodian transfers.
4. Produce a public transcript containing no secrets: ticket, purpose, roles, UTC window, algorithm, public key IDs, authority scopes/expiry, tool/hardware digests, exported public-record digests, deviations, and witness approvals.
5. Encrypt the full operational transcript for Security/Release/Audit and publish the approved public subset.
6. Auditor verifies the transcript and access logs before the authority is marked active.

## Rotation

Routine intermediate rotation follows the issuance section with a planned overlap, verifies both old and new paths on clean clients/devices, stops old-key signing, and publishes final use/expiry evidence. Root rotation additionally requires a separately approved transition design and dual-signed trust record; it must not silently replace the pinned root.

## Emergency Revocation

1. Security opens a critical incident and identifies exact key IDs, scopes, channels, sequences, and artifact digests.
2. Publication stops before root access begins.
3. Two custodians authorize the smallest valid emergency revocation statement.
4. Two clean hosts verify its signature and monotonically increasing sequence.
5. Release distributes it through every configured endpoint and confirms bridge/device cache uptake.
6. Security verifies that affected artifacts are quarantined and cannot start while evidence/source remain intact.
7. A replacement intermediate requires a new issuance record; restoring a suspected key is forbidden.

## Destruction

Key destruction requires a separate approved ticket, two custodians, Security, Release, and an Independent Witness. It occurs only after retention and historical-verification needs are satisfied, destroys every protected backup/factor under the hardware procedure, and records public key IDs and destruction evidence. Destruction is irreversible and is never used as an informal incident shortcut.
