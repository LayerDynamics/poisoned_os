# ADR-0005: Rust dependency policy

- **Status:** Accepted
- **Accepted:** 2026-08-21
- **Applies to:** Rust SDK, native FAP tooling, Wasm tooling, and the local builder

## Decision

Rust builds use the pinned toolchain and an exact `Cargo.lock`. Every resolved
package must be represented in `tools/rust/approved-crates.json` with its
registry or vendored source, checksum, license expression, enabled features,
unsafe-code disposition, reviewer, and reason for inclusion. The build uses
`--offline` and a checked-in vendor tree; git, path, registry replacement, and
network sources are rejected unless they are explicitly represented as a
content-addressed local source in the approval record.

The verifier rejects missing, extra, duplicated, yanked, revoked, or
license-denied packages, checksum changes, feature changes, and unapproved
source URLs. Updates require a new lockfile and review record. Proc macros and
build dependencies are subject to the same rules as runtime dependencies.

Unsafe Rust is permitted only in the reviewed SDK FFI/runtime crates. New
unsafe blocks require an invariant comment and reviewer in the approval
record; application crates remain safe by default.

## Consequences

An offline build can be reproduced from repository inputs without consulting a
registry. Dependency updates are more deliberate, but the exact source,
license, feature set, and safety review are auditable before bytes enter a
device artifact.
