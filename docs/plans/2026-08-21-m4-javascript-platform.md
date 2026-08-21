# M4 JavaScript Platform Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use lore:execute to implement this plan task-by-task.
> **Scope guard:** Do ONLY what is listed here. If you discover adjacent issues, record them in the deferred-findings section and continue. Do NOT fix them.

**Goal:** Let users edit, run, debug, package, and serve JavaScript directly on PoisonedOS while keeping device resources, native APIs, browser context, and credentials behind explicit capability boundaries.
**Architecture:** Harden the existing mJS runner into a managed workload runtime, replace unrestricted native FFI with capability-checked modules, and add a project/package contract shared by firmware and dashboard. Serve JavaScript interfaces from versioned bundles inside an isolated browser worker/iframe environment whose only device access is a validated message broker.
**Tech Stack:** C/Furi, mJS, nanopb/protobuf, TypeScript/React, Web Workers, sandboxed iframes, Content Security Policy, Vitest, Playwright, physical-device E2E.
**Practices:** Default deny, explicit budgets, deterministic packages, offline-first editing, structured logs/artifacts, sandbox escape testing, compatibility tests, approval-gated commits.
**Required skills:** `lore:execute`, `lore:test-driven-development`, `lore:testing`, `lore:security`, `lore:e2e-test-expert`, `lore:verification-before-completion`

---

## Achievement Ledger

| Achievement | Status before implementation | Required closing evidence |
|---|---|---|
| A4.1 Project/workload contracts | Not started | Cross-language manifest/lifecycle/bounds/current-previous compatibility fixtures pass. |
| A4.2 Capability-safe runtime | Not started | Every existing plugin is mapped and privilege/resource/termination attacks fail closed. |
| A4.3 Workload management | Not started | Ordered isolated console/artifact lifecycle survives retry, disconnect, cancel, and crash. |
| A4.4 Offline browser IDE | Not started | Versioned edit/validate/run/debug/package works after WAN removal. |
| A4.5 Served interfaces | Not started | Physical bundle delivery succeeds and every browser/device sandbox escape probe fails. |
| A4.6 Dependencies and physical workflow | Not started | Locked vendoring and full offline source-to-device workflow pass on hardware. |
| A4.7 Wasm decision | Not started | Physical measurements generate a reviewed ADR-0006 go/no-go result. |

**Canonical task commands:** firmware tasks run `./fbt FIRMWARE_APP_SET=unit_tests` and `python3 tools/hil/run_suite.py --suite firmware-units`; dashboard tests run `pnpm --dir dashboard verify`; JavaScript tool tests run `python3 -m unittest discover tools/javascript/tests`; physical/sandbox workflows run the exact HIL and Playwright commands named below. Record command, runtime/firmware/browser versions, physical IDs, exit code, and evidence digest before changing a ledger row.

## Achievement A4.1: JavaScript Project and Workload Contracts

### Task 1: Define manifests, lifecycle, console, and artifact schemas

**Files:**
- Create: `assets/protobuf/poison_workload.proto`
- Create: `assets/protobuf/poison_workload.options`
- Modify: `assets/protobuf/flipper.proto`
- Create: `data/poison/schemas/javascript-project.schema.json`
- Generate: `dashboard/src/generated/poison-workload.ts` through the M0 protocol generator
- Generate: `bridge/src/generated/poison_workload.rs` through the M0 protocol generator
- Create: `dashboard/src/workloads/javascript/manifest.ts`
- Test: `applications/debug/unit_tests/tests/js/poison_js_contract_test.c`
- Test: `dashboard/src/workloads/javascript/manifest.test.ts`

**Contract:**

```json
{
  "format": 1,
  "id": "org.poisonedos.script.example",
  "name": "Example Script",
  "version": "1.0.0",
  "language": "javascript",
  "entrypoint": "src/main.js",
  "runtime": "poison-mjs-1",
  "runtimeApi": 1,
  "firmwareApi": ">=1.0.0 <2.0.0",
  "capabilities": ["storage.project.read"],
  "limits": {
    "heapBytes": 32768,
    "wallTimeMs": 5000,
    "logBytes": 16384,
    "artifactBytes": 131072
  },
  "dependencies": "poison-js.lock",
  "servedUi": null
}
```

**Steps:**

1. Write failing compatibility tests for ID/version/language/entrypoint/runtime/runtime-API/firmware-API validation, project-version history, capability declarations, dependency lock, budgets, lifecycle states, logs, diagnostics, cancellation, artifacts, and served-UI metadata.
2. Define `Queued → Starting → Running → Completed/Failed/Canceled/Terminated` with exactly one terminal event.
3. Extend the M0 generator inputs and generate deterministic C, TypeScript, and Rust bindings; never hand-edit generated outputs. Freeze runtime identifier and unknown-field compatibility semantics.
4. Run firmware and dashboard contract tests → Expected: the same valid/invalid fixtures produce the same result.
5. Stage diffs and request approval for proposed `feat(javascript): define managed workload contracts`.

## Achievement A4.2: Capability-Safe Device Runtime

### Task 2: Replace unrestricted native access with approved modules

**Files:**
- Modify: `applications/system/js_app/js_thread.c:16-23,163-288,334-341`
- Modify: `applications/system/js_app/js_thread.h:1-49`
- Modify: `applications/system/js_app/js_modules.c:33-45,47-178`
- Modify: `applications/system/js_app/js_modules.h:1-47`
- Create: `applications/system/js_app/js_capabilities.c`
- Create: `applications/system/js_app/js_capabilities.h`
- Create: `applications/system/js_app/js_developer_policy.c`
- Create: `applications/system/js_app/js_developer_policy.h`
- Create: `applications/system/js_app/modules/js_poison_storage.c`
- Create: `applications/system/js_app/modules/js_poison_device.c`
- Create: `applications/system/js_app/modules/js_poison_evidence.c`
- Modify: `applications/system/js_app/application.fam`
- Create: `docs/development/javascript-capability-map.md`
- Test: `applications/debug/unit_tests/tests/js/poison_js_capabilities_test.c`

**Steps:**

1. Read the current mJS initialization/module registration and inventory every existing plugin declared in `applications/system/js_app/application.fam`: event loop, GUI and GUI views, notification, BadUSB, serial, GPIO, math, storage, and any additional plugin present when implementation starts.
2. Write failing tests for undeclared module import, raw pointer/native FFI access, cross-project file access, evidence mutation, revoked capability, capability escalation, unsigned developer mode, and developer-policy timeout/revocation.
3. Map every inventoried plugin API to named capabilities, parameter/output limits, lock/profile rules, and compatibility status; compatibility tests must prove allowed legacy calls or document and test an intentional stable-mode denial.
4. Disable general-purpose native FFI for untrusted projects and expose only bounded modules that call authenticated PoisonedOS services. Remove no existing module merely because it lacks a mapping: implement its mapping before stable mode can advertise compatibility.
5. Permit raw firmware-symbol FFI only for locally trusted signed code after Owner enables an explicit, time-bounded device-side developer policy through an M1 exact-target confirmation bound to signer, project digest, symbol scope, duration, and displayed native-code risk; audit activation, use, expiry, and revocation.
6. Resolve effective permissions as the intersection of signer policy, user role, project manifest, session state, developer policy, and device lock state.
7. Run JS and security unit suites → Expected: every current plugin has an explicit tested status, known scripts pass through supported modules, and every privilege bypass is rejected and audited.

### Task 3: Enforce resource limits and reliable termination

**Files:**
- Create: `applications/system/js_app/js_limits.c`
- Create: `applications/system/js_app/js_limits.h`
- Modify: `applications/system/js_app/js_thread.c:94-160,218-341`
- Modify: `lib/mjs/mjs_exec.c`
- Modify: `lib/mjs/mjs_exec.h`
- Modify: `lib/mjs/mjs_gc.c`
- Test: `applications/debug/unit_tests/tests/js/poison_js_limits_test.c`
- Create: `tools/hil/suites/javascript_limits.py`

**Steps:**

1. Test heap exhaustion, infinite loop, timer storm, oversized source, recursive import, log flood, artifact flood, cancellation race, and forced termination.
2. Add a bounded interpreter safe-point/fuel hook in the mJS bytecode execution loop so cancellation and wall-time checks occur during compute-only infinite loops, not only between callbacks; include the hook cost in M0 resource measurements.
3. Route all mJS allocations through a workload-accounted allocator or prove complete accounting at every mJS allocation site; enforce heap, source/module count and bytes, parser depth, stack/recursion, wall time, bytecode fuel, callbacks/timers, open handles, logs, and artifacts.
4. Clamp manifests to device policy maxima and emit an explicit terminal reason for each limit; killing the UI thread or rebooting is not successful termination.
5. Guarantee cleanup of timers, files, RPC streams, app sessions, and held hardware resources on every terminal path.
6. Run `python3 tools/hil/run_suite.py --suite javascript-limits` → Expected: the device remains responsive and can run a healthy script after each forced failure, including a compute-only infinite loop.

**Achievement check:** A hostile project cannot access undeclared APIs, persist after termination, or prevent device recovery without reflashing.

## Achievement A4.3: Workload Management, Console, and Artifacts

### Task 4: Add workload service and authenticated RPC

**Files:**
- Create: `applications/services/poison_workload/application.fam`
- Create: `applications/services/poison_workload/poison_workload.c`
- Create: `applications/services/poison_workload/poison_workload.h`
- Create: `applications/services/poison_workload/poison_workload_console.c`
- Create: `applications/services/poison_workload/poison_workload_artifact.c`
- Create: `applications/services/poison_workload/poison_workload_js_adapter.c`
- Create: `applications/services/poison_workload/poison_workload_js_adapter.h`
- Create: `applications/services/rpc/rpc_poison_workload.c`
- Test: `applications/debug/unit_tests/tests/workload/poison_workload_test.c`

**Steps:**

1. Write state-machine tests for queue, start, stdout, stderr, logs, exception/stack trace, typed events, generated files, cancel, timeout, crash, disconnect, reconnect, and artifact finalization.
2. Connect the service to the existing `js_thread` lifecycle through one adapter that owns context creation, project mount, capability resolution, start/cancel/forced termination, and cleanup; do not spawn an independent second mJS runner.
3. Implement bounded ordered console/event frames with monotonic sequence numbers, source/type metadata, and explicit truncation records.
4. Store completed artifacts through the M2 evidence service when requested; partial artifacts remain project-scoped and clearly marked.
5. Authorize create/run/cancel/inspect independently and audit every execution with project digest and effective capabilities.
6. Extend the M1 diagnostic registry with bounded JS start/terminal-reason/crash/limit/recovery counters; source, console, artifact, stack contents, and project names are prohibited diagnostic fields.
7. Run firmware workload, evidence, session, diagnostic-redaction, and support-bundle tests → Expected: retries are idempotent and one workload cannot observe another's console.

## Achievement A4.4: Offline Browser IDE

### Task 5: Build project editing, execution, debugging, and packaging flows

**Files:**
- Create: `dashboard/src/workloads/javascript/JavaScriptWorkspace.tsx`
- Create: `dashboard/src/workloads/javascript/ProjectExplorer.tsx`
- Create: `dashboard/src/workloads/javascript/CodeEditor.tsx`
- Create: `dashboard/src/workloads/javascript/RunControls.tsx`
- Create: `dashboard/src/workloads/javascript/Console.tsx`
- Create: `dashboard/src/workloads/javascript/Diagnostics.tsx`
- Create: `dashboard/src/workloads/javascript/PackageProject.ts`
- Create: `dashboard/src/workloads/javascript/SyntaxValidator.ts`
- Test: `dashboard/src/workloads/javascript/JavaScriptWorkspace.test.tsx`
- Test: `dashboard/src/workloads/javascript/PackageProject.test.ts`

**Steps:**

1. Test create/open/edit/save/version/restore, syntax validation, dirty/conflict state, offline reload, run/stop/rerun, diagnostics, console order, artifact save, capability review, and deterministic packaging.
2. Store authoritative project revisions under `/scripts/javascript/<project-id>/versions/` with an atomic current pointer; persist unsynchronized browser edits in IndexedDB without storing session secrets, and reconcile by revision/digest rather than last-writer-wins.
3. Validate the actual mJS-supported syntax and module graph before transfer; report file, line, column, stable diagnostic code, and unsupported-language feature without claiming full ECMAScript compatibility.
4. Use M2 transfer operations for project files and M4 workload RPC for execution; never infer success only from transport closure.
5. Package projects through the signed M3 format and show the exact source digest, runtime, limits, and capabilities.
6. Run `pnpm --dir dashboard verify` → Expected: complete edit/run/debug/package workflow passes offline after initial installation.

## Achievement A4.5: Safely Served JavaScript Interfaces

### Task 6: Implement versioned UI bundles and browser isolation

**Files:**
- Create: `applications/services/poison_workload/poison_js_bundle.c`
- Create: `applications/services/poison_workload/poison_js_bundle.h`
- Create: `applications/services/rpc/rpc_poison_js_bundle.c`
- Create: `dashboard/src/workloads/javascript/served/BundleVerifier.ts`
- Create: `dashboard/src/workloads/javascript/served/ServedAppHost.tsx`
- Create: `dashboard/src/workloads/javascript/served/served-worker.ts`
- Create: `dashboard/src/workloads/javascript/served/MessageBroker.ts`
- Create: `dashboard/src/workloads/javascript/served/csp.ts`
- Test: `dashboard/src/workloads/javascript/served/ServedAppHost.test.tsx`
- Test: `dashboard/e2e/javascript-sandbox.spec.ts`

**Steps:**

1. Define signed bundle metadata, immutable asset digests, entrypoint, API version, requested browser capabilities, and size limits.
2. Write escape tests for top/parent DOM access, cookies, local/session storage, IndexedDB, Cache API, service workers, BroadcastChannel, SharedWorker, network egress, credential access, bridge transport access, navigation, downloads, popups, clipboard, and forged/replayed broker messages.
3. Serve verified installed bundle bytes from the physical device through authenticated, capability-checked RPC with bounded chunking and digest validation.
4. Verify the complete bundle before loading it into a unique-origin iframe with `sandbox="allow-scripts"` only, backed by a dedicated worker; do not grant same-origin, forms, navigation, popups, downloads, pointer lock, or storage access.
5. Expose only typed, origin-bound, nonce-bound broker messages; reauthorize every device operation against the active session.
6. Apply `default-src 'none'`, digest-authorized local scripts/assets only, and `connect-src 'none'`; prohibit inline/eval execution unless a separately tested runtime requirement is recorded and narrowed in an ADR.
7. Serve each bundle from a non-secret immutable content address, revoke broker nonces on stop/session change, and prevent bundle code from reading dashboard IndexedDB or authentication material.
8. Run the browser test against bytes streamed from a physical device over each M1-supported transport, not a fixture server → Expected: allowed UI operations succeed and every isolation probe fails closed.

## Achievement A4.6: Offline Dependencies and Physical Workflow

### Task 7: Add deterministic dependency import and hardware E2E

**Files:**
- Create: `tools/javascript/vendor_package.py`
- Create: `data/poison/schemas/javascript-lock.schema.json`
- Create: `dashboard/src/workloads/javascript/DependencyManager.tsx`
- Create: `dashboard/e2e/javascript-workflow.spec.ts`
- Create: `tools/hil/suites/javascript_workflow.py`
- Create: `docs/testing/m4-javascript-protocol.md`
- Test: `tools/javascript/tests/test_vendor_package.py`

**Steps:**

1. Accept only source packages with a lock entry, content digest, license metadata, supported syntax/runtime declaration, and bounded dependency graph.
2. Vendor immutable dependency files into the project; the device runtime never fetches from the network.
3. On real hardware, create a project offline, add a vendored dependency, edit, run, observe logs, save an artifact, package, reinstall, and serve its isolated UI.
4. Repeat after USB and BLE disconnects, runtime termination, device reboot, and capability revocation.
5. Run `pnpm --dir dashboard e2e --grep @physical-javascript` and `python3 tools/hil/run_suite.py --suite javascript-workflow` → Expected: source-to-device and served-UI workflows pass without mock components.

## Achievement A4.7: Measured Wasm Architecture Decision

### Task 8: Benchmark and approve the runtime used by M5

**Files:**
- Create: `tools/bench/wasm_runtime_matrix.py`
- Create: `tools/bench/fixtures/wasm/compute.wat`
- Create: `tools/bench/fixtures/wasm/memory.wat`
- Create: `tools/bench/fixtures/wasm/host_calls.wat`
- Create: `tools/hil/suites/wasm_runtime_bench.py`
- Create: `docs/decisions/ADR-0006-rust-wasm-runtime.md`

**Steps:**

1. Evaluate candidate embedded interpreters against license/dependency policy, maintained status, Cortex-M4 code size, peak RAM, deterministic instruction/fuel metering, host-call control, trap safety, supported Wasm subset, reproducible build support, and capability adapter fit.
2. Compile committed `.wat` sources deterministically during the benchmark; do not commit opaque generated `.wasm` binaries as the source of truth.
3. Run identical compute, memory, host-call, cancellation, malformed-module, recursion, and exhaustion fixtures through isolated candidate adapters on physical Flipper Zero hardware.
4. Record source revision, compiler and flags, adapter digest, firmware digest, binary size, peak heap, startup, throughput, fuel behavior, cancellation latency, and post-trap recovery.
5. Select and pin one runtime only if it fits the SPEC budgets and fails closed; otherwise ADR-0006 records a no-go and M5 Wasm implementation is blocked for explicit scope revision while native Rust remains independently decidable.
6. Run `python3 tools/hil/run_suite.py --suite wasm-runtime-bench` and verify every ADR table cell is generated from its evidence artifact.

## M4 Exit Gate

```bash
./fbt lint_all
./fbt f7
./fbt FIRMWARE_APP_SET=unit_tests
python3 tools/hil/run_suite.py --suite firmware-units
pnpm --dir dashboard verify
pnpm --dir dashboard e2e --grep @javascript-sandbox
python3 tools/hil/run_suite.py --suite javascript-limits
python3 tools/hil/run_suite.py --suite javascript-workflow
python3 tools/hil/run_suite.py --suite wasm-runtime-bench
```

Expected: all exit `0`; FR-33–FR-40 pass, every current JS plugin has a tested capability/compatibility result, compute-only exhaustion recovers, served JavaScript cannot escape its browser or device capability sandbox, and ADR-0006 gives M5 a measured Wasm go/no-go decision. Show diffs and request approval before commits.

## Deferred Findings

Record out-of-scope findings here with evidence, impact, and owner. Do not implement them during M4.
