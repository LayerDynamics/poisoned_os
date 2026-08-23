# ADR-0006: Rust Wasm Runtime Selection

## Status

Pending physical measurement. No Wasm runtime is admitted to PoisonedOS until a candidate is measured on the supported Flipper target and reviewed against the M0 dependency policy and SPEC resource budgets.

## Decision boundary

The firmware currently exposes a fail-closed Wasm admission boundary in `lib/poison_wasm/`. It validates the Wasm magic/version, bounds module bytes, rejects forged or host-oriented imports, meters fuel/wall time/handles/logs/artifacts, and reports `PoisonWasmRuntimeUnavailable` because no measured runtime is installed. It must not execute modules through an unapproved substitute.

## Required evidence for a go decision

Candidate evidence must include an exact source revision, license, approved-crate record, target build digest, supported Wasm feature set, peak code/linear-memory/stack usage, host-call and fuel cost, trap/cleanup behavior, cancellation latency, and physical recovery results. The candidate must pass malformed-module, import-forgery, growth, recursion, cancellation, redaction, and post-trap healthy-workload tests.

## Consequence

Native Rust remains independently usable. Wasm packages are rejected at admission until this record is amended to `Accepted` with the complete measurement bundle; the current adapter is intentionally non-executing and cannot silently broaden the trust boundary.
