# ADR-0007: Evidence package format

## Decision

PoisonedOS V1 uses `poison.evidence-manifest/v1` as the canonical portable evidence manifest. Raw objects are content-addressed by lowercase SHA-256 and remain immutable after promotion. A package contains the manifest, raw objects, separately labeled derived objects, append-only annotations, audit history, per-object checksums, and an optional signature over the canonical manifest plus member digests.

Logical namespaces are stable and do not expose backing paths: `/system` is read-only; `/config` is internal with SD backup; `/profiles` is a versioned logical configuration namespace; `/apps`, `/scripts`, `/workloads`, `/cases`, `/evidence`, `/lessons`, and `/exports` are external logical namespaces; `/int` and `/ext` are explicit compatibility views.

Canonical JSON uses UTF-8, sorted object keys, no insignificant whitespace, lowercase hexadecimal digests, and integer millisecond timestamps. A current reader must reject unknown required schema versions, malformed bounds, digest mismatches, duplicate archive members, and broken audit ancestry. Import first enters quarantine; acceptance occurs only after all structure, signatures, object digests, and cross-references verify.

The authenticated device RPC prepares a manifest from strictly sorted, unique evidence IDs in sequential batches of at most eight IDs so every request remains inside the 768-byte secure payload bound. The device persists the verified ID sequence atomically and returns its lowercase SHA-256 receipt; its `signature` field is empty because portable package signing belongs to the bridge/export authority and remains optional for this V1 format.

## Consequences

- A preview or annotation never changes a raw object or its digest.
- Repeated export of the same verified records has byte-identical member order and manifest bytes.
- The bridge index is derived and rebuildable; it is never the source of truth.
- V1 supports only the bounded fields in `schemas/poison/evidence-manifest.schema.json`; later versions require an explicit compatibility decision.
