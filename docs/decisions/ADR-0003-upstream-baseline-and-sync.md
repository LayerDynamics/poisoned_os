# ADR-0003: Upstream Baseline and Synchronization

- **Status:** Accepted
- **Decision owner:** Firmware and Release
- **Accepted:** 2026-08-21
- **Applies to:** PoisonedOS V1 and all M0–M6 work

## Context

PoisonedOS needs a conservative firmware base while adding authenticated
browser control, evidence workflows, curated professional tools, classroom
operation, and managed JavaScript and Rust workloads. The measured firmware
comparison shows that official Flipper firmware and Momentum retain the same
Furi, FAP, storage, RPC, protocol-library, and FBT/SCons foundations, but
Momentum changes 805 common files and many product-defining policies across
boot, UI, locking, radio, protocols, applications, SDK exports, packaging, and
release automation. Momentum is therefore valuable implementation and migration
evidence, but it is not a safe second base to merge wholesale.

The imported workspace also contains the complete contents of the official
firmware's twelve dependency gitlinks without nested product Git repositories.
Leaving those directories untracked while restoring a submodule-oriented root
would make build and provenance behavior depend on the operator's local Git
state.

## Decision

### Official baseline

PoisonedOS uses `flipperdevices/flipperzero-firmware` branch `dev` at commit
`a55e39395ff31bd5fdf3929c70720a7fb76e5968` as its V1 baseline. The immutable
comparison checkout remains at `do_not_include/flipperzero-firmware`; it is
read-only evidence and is excluded from product history, packages, and SBOMs.

Momentum commit `d3f89dfe2ef6b01839201598e9be1590cba80322`
is an implementation reference and legacy-migration source. Momentum code or
data enters PoisonedOS only through a requirement-owned port with provenance,
license review, compatibility tests, and the milestone's security gates.

### Dependency mode

All twelve official gitlink dependencies are fully vendored in the PoisonedOS
product tree at the commits recorded in `provenance/baseline.lock.json`. The
root intentionally does not restore the upstream `.gitmodules` contract:
`provenance/baseline.lock.json`, the component inventory, and generated SBOMs
are the authoritative replacement. Builds must work without `git submodule`
network access, nested Git metadata, or an ambient dependency checkout.

The root and the two dependencies that carry their own attribute files disable
Git text normalization across vendored dependency contents. This is deliberate:
the lock records checked-out bytes, including upstream CRLF/LF choices, and a
fresh PoisonedOS checkout must reproduce those bytes rather than reinterpret
them through the former submodules' independent attribute contexts. The two
dependency `.gitattributes` files are the only vendor metadata adaptations.

Dependency updates require one reviewable change that updates the vendored
bytes, commit pin, source URL, license evidence, component digest, SBOM fixture,
and relevant compatibility/security tests together. A partial update is
invalid. Dependency directories may not be silently converted back to
submodules.

### Upstream cadence and intake

The firmware owner reviews official `dev` changes monthly and before every
milestone exit. Published official security advisories are triaged within one
business day; applicable critical fixes enter an emergency review immediately,
and applicable high-severity fixes enter the next release candidate unless the
security owner documents a release-blocking reason.

Each intake starts by advancing only the read-only comparison checkout in a
dedicated review, regenerating `provenance/upstream-paths.json`, and inspecting
every changed classification. The product tree changes only after the new
upstream commit, dependency pins, license state, API delta, protocol delta,
radio-policy delta, and official parity results are approved.

### Merge and history policy

PoisonedOS does not rebase published product history and does not merge the
comparison clone or Momentum history into the product branch. Approved upstream
changes are applied as explicit, reviewable product commits that preserve
PoisonedOS modifications. Each sync records the prior and new official commits
and links the generated path classification. Security backports are separate
commits so their scope remains auditable.

### Conflict ownership

- Firmware owns boot, Furi services, power/input, target HAL, build integration,
  updater behavior, and dependency conflicts.
- Security owns pairing, session/RPC policy, signing, revocation, locked-device
  behavior, radio-policy enforcement, sandbox boundaries, and cryptography.
- Data owns storage, VFS, migration, evidence formats, indexing, and recovery.
- Dashboard/Bridge owns generated clients, transport adapters, browser origin
  policy, and local service compatibility.
- Product jointly approves user-visible workflow or policy changes; Legal joins
  every licensing or distribution conflict.

No owner may resolve a conflict by dropping a called symbol, generated binding,
protocol field, or apparently unused dependency. The implementation and all
callers must be reconciled and verified.

### API compatibility policy

The generated F7 API symbol table and canonical protobuf definitions are public
compatibility surfaces. Within V1, upstream intake may add compatible symbols,
optional fields, or enum values, but may not remove or silently change an
accepted PoisonedOS symbol, field number, bound, capability, or error semantic.
Breaking changes require a new major contract, current/previous compatibility
fixtures, migration behavior, and explicit Product, Firmware, and Security
approval.

## Verification

`provenance/upstream-paths.json` assigns exactly one of `identical`,
`poison-modified`, `poison-added`, `upstream-omitted`, or `dependency` to every
reported path. The report uses modes from the official Git tree and SHA-256 of
checked-out bytes; comparison-repository internals never enter product
provenance.

The local gate is:

```bash
python3 -m unittest tools.tests.test_compare_upstream
python3 tools/compare_upstream.py --check
```

## Consequences

The product starts from the smaller vendor-maintained surface and can audit each
deliberate divergence. Fully vendored dependencies make offline builds and
license inventory deterministic, at the cost of larger product history and an
explicit update procedure. Momentum features require selective ports instead of
bulk merges, which adds review work but prevents fork-wide radio, security, UI,
and packaging policy from arriving implicitly.
