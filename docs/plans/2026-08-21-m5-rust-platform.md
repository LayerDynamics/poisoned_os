# M5 Rust Platform Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use lore:execute to implement this plan task-by-task.
> **Scope guard:** Do ONLY what is listed here. If you discover adjacent issues, record them in the deferred-findings section and continue. Do NOT fix them.

**Goal:** Give advanced users a reproducible Rust source-to-device workflow for both signed native FAPs and sandboxed Wasm workloads, controllable from the same offline browser workspace used for JavaScript.
**Architecture:** Publish a versioned Rust SDK over a deliberately small generated device ABI, compile in an isolated local builder with an optional policy-equivalent hosted endpoint, and emit signed M3 packages with detached provenance. Native output is integrated into the existing FAP metadata, SDK-symbol, relocation, validation, and packaging pipeline; Wasm output runs through the resource-metered firmware adapter selected by M4 ADR-0006.
**Tech Stack:** Rust/Cargo, `thumbv7em-none-eabihf`, WebAssembly, C/Furi, Docker/OCI sandbox, TypeScript/React, protobuf, property tests, fuzzing, physical-device E2E.
**Practices:** Decision records before dependency commitment, locked toolchains, hermetic builds, no ambient builder network, explicit capabilities, unsafe-code containment, reproducibility checks, approval-gated commits.
**Required skills:** `lore:execute`, `lore:test-driven-development`, `lore:testing`, `lore:security`, `lore:e2e-test-expert`, `lore:verification-before-completion`

---

## Achievement Ledger

| Achievement | Status before implementation | Required closing evidence |
|---|---|---|
| A5.1 Dependency boundary | Not started | ADR-0005 and offline vendor/license/source verification pass. |
| A5.2 Versioned SDK/runtime | Not started | C/Rust ABI layout, allocator, panic, teardown, and capability tests pass. |
| A5.3 Hermetic builder | Not started | API/idempotency, sandbox escape, restart, provenance, and reproducibility tests pass. |
| A5.4 Native FAP target | Not started | Rust artifacts traverse normal/fast existing FAP pipelines and launch on hardware. |
| A5.5 Sandboxed Wasm | Not started | ADR-0006 runtime adapter traps, meters, cleans up, and denies forged imports. |
| A5.6 Browser Rust workspace | Not started | Offline revision/edit/build/diagnose/install/run UI passes for both targets. |
| A5.7 Source-to-device workflow | Not started | Native/Wasm reproducibility, attacks, crash recovery, and evidence pass end to end. |

**Canonical task commands:** SDK tasks run `cargo test --workspace --manifest-path rust-sdk/Cargo.toml` and `cargo clippy --manifest-path rust-sdk/Cargo.toml -- -D warnings`; builder tasks run `cargo test --workspace --manifest-path builder/Cargo.toml`; bridge and dashboard tasks run `cargo test --workspace --manifest-path bridge/Cargo.toml` and `pnpm --dir dashboard verify`; firmware and physical tasks run the exact FBT/HIL commands named below. Record command, toolchain/container/source digests, physical IDs, exit code, and evidence digest before changing a ledger row.

## Achievement A5.1: Approved Rust Dependency Boundary

### Task 1: Decide dependency sources, review, and update policy before building

**Files:**
- Create: `docs/decisions/ADR-0005-rust-dependency-policy.md`
- Create: `docs/security/rust-supply-chain.md`
- Create: `tools/rust/verify_vendor.py`
- Create: `tools/rust/approved-crates.json`
- Test: `tools/rust/tests/test_verify_vendor.py`

**Steps:**

1. Inventory crates/tools required by the SDK, proc macros, native target, Wasm target, builder, packaging, SBOM, and M4-selected runtime adapter.
2. In ADR-0005, decide allowed registries/source types, lock/vendor requirements, checksum and signature handling, license policy, unsafe-code review, maintainer/abandonment thresholds, advisory response, yanked/revoked handling, and update approval.
3. Require every dependency to have exact source revision/version, checksum, license, feature set, transitive closure, unsafe-code disposition, reviewer, and reason; reject undeclared git/path/network sources.
4. Implement offline verification of `Cargo.lock`, vendor contents, approved-crate metadata, licenses, and source checksums.
5. Run the verifier against valid, tampered, missing, extra, yanked/revoked, license-denied, feature-changed, and unapproved-source fixtures.
6. Stage diffs and request approval for proposed `docs(rust): approve dependency and supply-chain policy`.

**Achievement check:** Every byte that may enter a Rust build is pinned, reviewable, license-accounted, and usable without an ambient network connection.

## Achievement A5.2: Versioned Rust SDK

### Task 2: Implement the stable device API and SDK crates

**Files:**
- Create: `rust-toolchain.toml`
- Create: `rust-sdk/Cargo.toml`
- Create: `rust-sdk/crates/poison-sdk/Cargo.toml`
- Create: `rust-sdk/crates/poison-sdk/src/lib.rs`
- Create: `rust-sdk/crates/poison-sdk/src/storage.rs`
- Create: `rust-sdk/crates/poison-sdk/src/device.rs`
- Create: `rust-sdk/crates/poison-sdk/src/evidence.rs`
- Create: `rust-sdk/crates/poison-sdk/src/ui.rs`
- Create: `rust-sdk/crates/poison-macros/Cargo.toml`
- Create: `rust-sdk/crates/poison-macros/src/lib.rs`
- Create: `applications/services/poison_rust_api/poison_rust_api.h`
- Create: `applications/services/poison_rust_api/poison_rust_api.c`
- Create: `schemas/poison/rust-device-api.yaml`
- Create: `tools/protocol/generate_rust_device_api.py`
- Generate: `rust-sdk/crates/poison-sdk/src/generated.rs`
- Test: `rust-sdk/crates/poison-sdk/tests/api_contract.rs`
- Test: `applications/debug/unit_tests/tests/rust/poison_rust_api_test.c`

**Steps:**

1. Define calling convention, integer widths/alignment, ABI/API versioning, status values, opaque handles, UTF-8/byte slice pointer-length rules, ownership/borrowing, allocation/free pairing, cancellation, event delivery, reentrancy, panic/trap behavior, and capability checks.
2. Generate C and Rust declarations from `schemas/poison/rust-device-api.yaml` using the M0 deterministic-generation conventions; assert size, alignment, offset, constant, symbol, and calling-convention parity in both languages and prohibit hand edits.
3. Expose safe wrappers for project storage, device metadata, structured UI, logs, cancellation, and evidence submission; keep raw Furi symbols outside the supported SDK.
4. Confine unavoidable `unsafe` code to reviewed FFI modules with documented invariants and deny unsafe code elsewhere.
5. Run `cargo test --workspace --manifest-path rust-sdk/Cargo.toml`, `cargo clippy --manifest-path rust-sdk/Cargo.toml -- -D warnings`, and firmware API tests → Expected: PASS with ABI snapshots stable.

### Task 3: Implement native startup, allocation, panic, and teardown contracts

**Files:**
- Create: `rust-sdk/crates/poison-runtime/Cargo.toml`
- Create: `rust-sdk/crates/poison-runtime/src/lib.rs`
- Create: `rust-sdk/crates/poison-runtime/src/entry.rs`
- Create: `rust-sdk/crates/poison-runtime/src/allocator.rs`
- Create: `rust-sdk/crates/poison-runtime/src/panic.rs`
- Test: `rust-sdk/crates/poison-runtime/tests/runtime_contract.rs`
- Test: `applications/debug/unit_tests/tests/rust/poison_rust_runtime_test.c`

**Steps:**

1. Define the exported entry symbol and return status expected by the FAP loader, stack source/limit, heap allocator ownership, out-of-memory behavior, panic behavior with unwinding disabled, static initialization, and destructor/resource cleanup order.
2. Test zero allocation, exhaustion, double free protection at the host boundary, panic before/after service acquisition, cancellation, normal return, and repeated launches.
3. Route allocations and handles through workload accounting so native-app limits, logs, crash reports, and cleanup share the M4 lifecycle semantics where applicable.
4. Preserve source/project revisions and finalized evidence after a native crash; write crash diagnostics as a separate bounded artifact without secrets or raw case contents.
5. Run Rust runtime and firmware loader tests → Expected: every terminal path returns device resources and a healthy native app launches after each injected failure.

## Achievement A5.3: Hermetic Rust Builder and Provenance

### Task 4: Implement authenticated isolated build jobs

**Files:**
- Create: `builder/Cargo.toml`
- Create: `builder/Cargo.lock`
- Create: `builder/src/main.rs`
- Create: `builder/src/api.rs`
- Create: `builder/src/job.rs`
- Create: `builder/src/policy.rs`
- Create: `builder/src/provenance.rs`
- Create: `builder/src/sandbox.rs`
- Create: `builder/src/store.rs`
- Create: `builder/src/stream.rs`
- Create: `builder/src/diagnostics.rs`
- Create: `builder/container/Dockerfile`
- Create: `builder/container/seccomp.json`
- Test: `builder/tests/job_isolation.rs`
- Test: `builder/tests/reproducible_build.rs`
- Test: `builder/tests/provenance.rs`

**Steps:**

1. Define and test exact APIs for builder version/capabilities, create job, upload source/vendor chunks, finalize inputs, start, status, ordered log/diagnostic stream, cancel, artifact/provenance download, and expiration; every mutating retry carries an idempotency key.
2. Write tests for authenticated submission, authorization, cross-user/project isolation, CPU/memory/time/process/output limits, filesystem/device/socket escape, network denial, dependency tampering, cancellation races, log truncation, cleanup, restart recovery, and expired jobs.
3. Pin Rust toolchain, target components, Cargo dependencies, SDK, linker tools, Wasm runtime tools, and container base by digest.
4. Build with read-only verified inputs, an empty writable job directory, no ambient network, no host credentials/devices, and a minimal seccomp/capability policy; dependencies arrive only as the verified lockfile plus content-addressed vendor bundle.
5. Emit source, dependency-lock, vendor, toolchain-image, compiler, SDK/API, target, flags, capabilities, dependency-license, output, and builder-policy digests as signed provenance. Put wall-clock build/signing timestamps in a detached signed attestation excluded from reproducible artifact bytes.
6. Build the same fixture twice in clean sandboxes and compare output bytes and deterministic provenance; test that timestamp changes affect only the detached attestation.
7. Emit bounded M1-compatible builder queue/start/terminal/failure/limit counters without source, compiler output, user/project names, dependency contents, or stable hosted identity; test redaction and support-bundle inclusion.
8. Run `cargo test --workspace --manifest-path builder/Cargo.toml` and the container isolation suite → Expected: all escape probes fail, retries do not duplicate builds, and clean builds are byte-identical.

## Achievement A5.4: Signed Native FAP Target

### Task 5: Integrate the native target with the existing FAP pipeline

**Files:**
- Create: `rust-sdk/targets/thumbv7em-poison-fap.json`
- Create: `rust-sdk/linker/poison-fap-link`
- Create: `rust-sdk/crates/poison-build/Cargo.toml`
- Create: `rust-sdk/crates/poison-build/src/lib.rs`
- Create: `rust-sdk/examples/native-hello/Cargo.toml`
- Create: `rust-sdk/examples/native-hello/src/main.rs`
- Modify: `scripts/fbt_tools/fbt_extapps.py`
- Modify: `scripts/fbt/elfmanifest.py`
- Modify: `site_scons/extapps.scons`
- Modify: `scripts/fastfap.py`
- Modify after M3 Task 1 creates it: `tools/packages/build_package.py`
- Create: `bridge/src/workloads/rust_artifact.rs`
- Create: `applications/services/poison_workload/poison_workload_native_adapter.c`
- Create: `applications/services/poison_workload/poison_workload_native_adapter.h`
- Test: `rust-sdk/crates/poison-build/tests/native_package.rs`
- Test: `bridge/tests/rust_artifact_acceptance.rs`
- Test: `tools/hil/suites/rust_native.py`

**Steps:**

1. Derive target CPU, hard-float ABI, memory, relocation, entry/export, stack, SDK-symbol, metadata, and linker requirements from the current firmware and FAP build settings; do not copy assumptions from another board.
2. Extend the existing external-app build path so Rust object/archive inputs go through the same `.fapmeta` manifest assembly, SDK import validation, ELF compaction/relocation, hardware/API checks, asset handling, debug artifact, and fast-build path as C FAPs.
3. Reject unsupported symbols/relocations, unwinding, undeclared allocation policy, mutable global constructors, entry mismatch, stack overflow budget, hardware target mismatch, and ABI/API mismatch before packaging.
4. Convert/link the Rust artifact into a normal validated FAP, then sign its M3 package; do not create a second native loader or bypass `FlipperApplicationPreloadStatus` validation.
5. Add a native workload adapter to the M4 workload service that delegates launch/stop to the existing FAP loader/app service and translates the M3 structured SDK events, logs, diagnostics, and artifacts into the shared lifecycle; it must not load ELF/FAP bytes itself.
6. Validate every locally built or precompiled Rust-derived artifact's signature, manifest, target, API/ABI version, SDK import set, provenance, capability declaration, and digest before the dashboard offers installation.
7. Build the native example through both normal and fast FAP paths, compare metadata/imports/relocations, install it, launch it, exercise structured UI, trigger a controlled panic, and save evidence on physical hardware.
8. Run Rust SDK, FAP tool, loader, bridge acceptance, and `python3 tools/hil/run_suite.py --suite rust-native` tests → Expected: deterministic signed output launches through the existing loader and any invalid acceptance field blocks installation.

## Achievement A5.5: Sandboxed Wasm Execution

### Task 6: Integrate the M4-selected runtime behind a narrow adapter

**Files:**
- Create: `lib/poison_wasm/poison_wasm.c`
- Create: `lib/poison_wasm/poison_wasm.h`
- Create: `lib/poison_wasm/poison_wasm_host.c`
- Create: `lib/poison_wasm/poison_wasm_host.h`
- Create: `lib/poison_wasm/poison_wasm_limits.c`
- Create: `lib/poison_wasm/poison_wasm_limits.h`
- Create: `applications/services/poison_workload/poison_workload_wasm_adapter.c`
- Create: `applications/services/poison_workload/poison_workload_wasm_adapter.h`
- Create: `rust-sdk/examples/wasm-hello/Cargo.toml`
- Create: `rust-sdk/examples/wasm-hello/src/lib.rs`
- Test: `applications/debug/unit_tests/tests/rust/poison_wasm_test.c`
- Test: `tools/hil/suites/rust_wasm.py`

**Steps:**

1. Confirm ADR-0006 is a measured `go` and add its exact runtime revision/license using ADR-0005 and the M0 dependency policy; if it is `no-go`, do not substitute an unmeasured runtime.
2. Write tests for malformed modules, unsupported features, import forgery, memory/table growth, fuel exhaustion, recursion, host-call flooding, capability revocation, cancellation, and cleanup after trap.
3. Expose only versioned `poison_*` host imports whose implementations delegate to the same capability-checked services as JavaScript.
4. Connect the runtime to the M4 workload service through one Wasm adapter for admission, project mount, host-import resolution, start/cancel, event/artifact forwarding, trap reporting, and cleanup.
5. Enforce module size, linear memory, stack, fuel, wall time, handles, log, and artifact limits.
6. Extend workload diagnostics with bounded native panic/Wasm trap/limit/recovery reason codes, never source, stack contents, case data, or raw generated artifacts.
7. Run unit, fuzz, diagnostic-redaction, support-bundle, and physical HIL suites → Expected: healthy workloads pass after every forced trap and no module escapes its project or declared capabilities.

## Achievement A5.6: Browser Rust Workspace

### Task 7: Add source, dependency, build, install, diagnostics, and run flows

**Files:**
- Create: `dashboard/src/workloads/rust/RustWorkspace.tsx`
- Create: `dashboard/src/workloads/rust/CargoEditor.tsx`
- Create: `dashboard/src/workloads/rust/BuildPanel.tsx`
- Create: `dashboard/src/workloads/rust/ProvenanceView.tsx`
- Create: `dashboard/src/workloads/rust/TargetSelector.tsx`
- Create: `dashboard/src/workloads/rust/Diagnostics.tsx`
- Create: `dashboard/src/workloads/rust/RustProjectStore.ts`
- Create: `dashboard/src/workloads/rust/ProjectImport.tsx`
- Create: `bridge/src/builder/mod.rs`
- Create: `bridge/src/builder/client.rs`
- Create: `bridge/src/builder/local.rs`
- Test: `dashboard/src/workloads/rust/RustWorkspace.test.tsx`
- Test: `dashboard/src/workloads/rust/ProvenanceView.test.tsx`
- Test: `dashboard/src/workloads/rust/RustProjectStore.test.ts`
- Test: `dashboard/src/workloads/rust/ProjectImport.test.tsx`
- Test: `bridge/tests/builder_client.rs`

**Steps:**

1. Test single `.rs` import into a deterministic minimal Cargo project, complete Cargo project/archive import, native/Wasm project creation, revision history/restore, locked dependency import, offline edits, conflict recovery, queued build, streamed progress/stdout/stderr/structured diagnostics, cancellation, builder restart, compiler failure, native panic/Wasm trap diagnostics, provenance verification, precompiled-artifact admission, install, deploy/start/stop/rerun, and generated-artifact capture.
2. Store authoritative immutable source revisions under `/workloads/rust/<project-id>/versions/` with manifest, Cargo lock/vendor references, source digest, parent revision, author, and atomic current pointer; store generated build artifacts by separate output digest and never overwrite source with compiler output.
3. Use the bridge-managed local builder by default, pin and authenticate its loopback endpoint, stream the Task 4 API without buffering unbounded logs, and stop child jobs on bridge shutdown according to the job retention policy. A hosted builder is an explicitly configured policy-equivalent endpoint and never required for local workflows.
4. Require the user to review target, capabilities, trust level, signer, source/output digests, and provenance, then bind an M1 exact-target confirmation token to the native artifact digest and declared capabilities before installation and again before first execution if policy requires it.
5. Reject approval reuse after source, artifact, manifest, capability, signer, provenance, or device-policy change; add caller-specific confirmation regressions to the M1 coverage registry.
6. Reuse M4 console/artifact UI and M3 package install rather than creating parallel execution or trust paths.
7. Run `pnpm --dir dashboard verify` → Expected: both targets complete typed UI flows, source revisions remain independent, invalid provenance blocks install, and changed native targets require new approval.

## Achievement A5.7: Complete Source-to-Device Rust Workflow

### Task 8: Prove native and Wasm workflows on hardware

**Files:**
- Create: `dashboard/e2e/rust-workflow.spec.ts`
- Create: `tools/hil/suites/rust_workflow.py`
- Create: `docs/testing/m5-rust-protocol.md`

**Steps:**

1. From a clean offline environment, create the same Rust project for native and Wasm targets, vendor dependencies, build, verify provenance, sign, install, run, inspect logs, and save evidence.
2. Rebuild both targets from identical inputs and compare bytes; change one source byte and prove both source and output digests change.
3. Attempt network access, undeclared storage access, resource exhaustion, bad signature, bad provenance, and cancellation for both targets.
4. Reboot the physical device after forced failures and rerun a known-good native app and Wasm workload.
5. Run `pnpm --dir dashboard e2e --grep @physical-rust` and `python3 tools/hil/run_suite.py --suite rust-workflow` → Expected: no mock component, reproducible output, enforced capabilities, and clean recovery.

## M5 Exit Gate

```bash
./fbt lint_all
./fbt f7
./fbt FIRMWARE_APP_SET=unit_tests
python3 tools/hil/run_suite.py --suite firmware-units
cargo test --workspace --manifest-path rust-sdk/Cargo.toml
cargo clippy --manifest-path rust-sdk/Cargo.toml -- -D warnings
cargo test --workspace --manifest-path builder/Cargo.toml
python3 tools/rust/verify_vendor.py --locked
pnpm --dir dashboard verify
python3 tools/hil/run_suite.py --suite rust-native
python3 tools/hil/run_suite.py --suite rust-wasm
python3 tools/hil/run_suite.py --suite rust-workflow
```

Expected: all exit `0`; FR-41–FR-50 pass, dependency policy is enforced, native artifacts traverse the existing FAP pipeline, native and Wasm outputs are reproducible with timestamps detached, sources survive failures, and builder/runtime escape tests fail closed on real hardware. Show diffs and request approval before commits.

## Deferred Findings

Record out-of-scope findings here with evidence, impact, and owner. Do not implement them during M5.
