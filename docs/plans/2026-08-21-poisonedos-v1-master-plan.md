# PoisonedOS V1 Master Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use lore:execute to implement this plan task-by-task.
> **Scope guard:** Do ONLY what is listed here. If you discover adjacent issues, record them in the deferred-findings section and continue. Do NOT fix them.

**Goal:** Deliver SPEC-1 through seven independently gated milestones, ending in a secure, supportable PoisonedOS V1 release.
**Architecture:** Preserve the existing Furi/FreeRTOS firmware core and extend it with authenticated RPC, VFS/evidence services, structured applications, and managed JavaScript/Rust workloads. Pair it with an offline TypeScript PWA, a local Rust bridge/build path, and optional hosted services that never become mandatory for local operation.
**Tech Stack:** C/C++, Furi/FreeRTOS, nanopb/protobuf, mbedTLS, Python/FBT/SCons, TypeScript/PWA, Rust/Tokio, Playwright, hardware-in-loop Flipper Zero devices.
**Practices:** Contract-first public protocols, typed-first schemas, TDD for security and state machines, property testing for persistence, real-hardware E2E, reproducible builds, approval-gated commits.
**Required skills:** `lore:test-driven-development`, `lore:security`, `lore:e2e-test-expert`, `lore:verification-before-completion`, `lore:verify-before-documenting`

---

## Source of Truth

- Product specification: `docs/specs/SPEC-1-poisonedos-for-the-flipper-zero.md`
- Firmware comparison: `documentation/development/Momentum_Vs_Flipper.md`
- Verified official baseline snapshot: `do_not_include/flipperzero-firmware` at `a55e39395ff31bd5fdf3929c70720a7fb76e5968`
- Current constraint: the workspace root is not a Git worktree and lacks normal root build/release entrypoints; M0 resolves this before feature implementation.

## Baseline Strategy Derived from the Firmware Comparison

PoisonedOS starts from the verified official-firmware snapshot because it is the vendor-maintained, smaller, more conservative SDK/CI/radio-policy baseline. Momentum is an implementation reference and migration source, not a second base to merge wholesale. This follows the comparison's evidence that Momentum modifies boot, global settings, desktop/GUI, Archive, power/input, RPC locking, radio HAL, protocol registries, hardware applications, JS modules, SDK exports, build defaults, packaging, and release automation while adding a large community FAP and asset-pack surface.

| Comparison finding | PoisonedOS plan consequence |
|---|---|
| OFW and Momentum still share a large Furi/FAP/storage/RPC architecture | Preserve existing low-level firmware services and integrate through explicit service/adapter boundaries rather than replacing the platform. |
| Momentum's global settings, asset packs, menus/status presentation, keybinds, Archive enhancements, and expanded app UX demonstrate customization demand | M3 implements signed, versioned, previewable, atomic profiles/assets and a real browser file/tool experience, with a built-in known-good recovery profile. |
| Momentum changes radio policy and contains an explicit extended-range/region-bypass path | PoisonedOS never imports a bypass. Region/hardware/classroom policy is rechecked in firmware before every transmit-class action and cannot be weakened by a theme, profile, browser, or catalog entry. |
| Momentum expands NFC/LF RFID/iButton/infrared/GPIO/Sub-GHz/USB/BLE workflows and protocols | M3 uses the comparison to drive ten independently reviewed adapters, but only hardware-supported, licensed, tested, documented workflows enter the curated V1 catalog. |
| Momentum expands JavaScript modules and raw developer access | M4 inventories every current OFW plugin and uses Momentum only as API/design evidence; stable PoisonedOS exposes capability-checked modules and confines raw FFI to explicit time-bounded trusted developer policy. |
| Momentum pins a large community application submodule and exports a larger firmware API | M0/M3/M5 require provenance, license, capability, API-compatibility, and signature review. No third-party app, API symbol, protocol, dataset, or asset becomes supported merely because Momentum ships it. |
| The two snapshots contain different paths, formats, settings, assets, and app bundles | M2 inventories both OFW and Momentum-derived device data read-only, converts only proven formats, preserves raw legacy bytes, and guarantees rollback. |

The exhaustive path/directory manifests in `documentation/development/Momentum_Vs_Flipper.md` are therefore inputs to M0 provenance classification, M2 migration fixtures, M3 tool/profile selection, and M4 compatibility tests. They are not a license to copy every fork-specific file into PoisonedOS.

## Execution Order

| Order | Milestone | Plan | Achievement Gate |
|---:|---|---|---|
| 1 | M0 — Reproducible Baseline | `docs/plans/2026-08-21-m0-baseline-and-governance.md` | Firmware builds reproducibly; provenance, licenses, SBOM, CI, and hardware inventory validate |
| 2 | M1 — Secure Physical-Device Control | `docs/plans/2026-08-21-m1-secure-browser-device-control.md` | Real browser pairs, streams, controls, and launches apps on real USB/BLE hardware |
| 3 | M2 — Files and Evidence MVP | `docs/plans/2026-08-21-m2-unified-files-and-evidence.md` | Transactional capture/export survives fault injection and verifies byte-for-byte |
| 4 | M3 — Professional UI, Updates, Recovery, and Curated Tools | `docs/plans/2026-08-21-m3-apps-tools-education-customization.md` | Signed content/firmware rollback, device recovery, apps, profiles, lessons, and all ten audited tool families pass |
| 5 | M4 — JavaScript Platform | `docs/plans/2026-08-21-m4-javascript-platform.md` | Offline edit/run/serve/debug works with capability and sandbox tests passing |
| 6 | M5 — Rust Platform | `docs/plans/2026-08-21-m5-rust-platform.md` | Reproducible native/Wasm source-to-device workflows pass on hardware |
| 7 | M6 — Stable V1 | `docs/plans/2026-08-21-m6-hardening-and-stable-release.md` | All 58 MUST requirements and all release gates pass with zero critical/high defects |

## Schedule and Accountability Baseline

No calendar deadline or team size is committed. These are elapsed target windows from SPEC-1 and must be re-estimated after M0 from measured build, HIL, dependency, licensing, and staffing evidence; re-estimation changes dates, never exit criteria.

| Milestone | Target window | Accountable discipline | Required review disciplines |
|---|---:|---|---|
| M0 | Weeks 0–4 | Firmware/Release | Legal/licensing, Security, HIL operations |
| M1 | Weeks 5–12 | Firmware/Dashboard | Security, Bridge/platform, Accessibility |
| M2 | Weeks 13–22 | Firmware/Data | Security, Dashboard, Education, Evidence-format review |
| M3 | Weeks 23–34 | Product/Firmware | Security, Education, RF/regulatory, Accessibility |
| M4 | Weeks 35–42 | Runtime/Dashboard | Security, Browser sandbox, Firmware resource review |
| M5 | Weeks 43–54 | Toolchain/Runtime | Security, Firmware ABI/FAP, Supply chain |
| M6 | Weeks 55–62 | Release/Security | Independent rebuild, Accessibility/usability, Operations |

Each accountable discipline names an individual owner and backup in milestone evidence before implementation starts. M6 cannot compensate for a missing owner in an earlier milestone.

## Dependency Rules

```text
M0 repository/toolchain/codegen/signing/decisions
 └─► M1 session/policy/audit/confirmation/diagnostics/control
      └─► M2 VFS/migration/cases/evidence/offline index
           └─► M3 packages/updates/recovery/tools/profiles/lessons
                └─► M4 JavaScript/served UI/Wasm-runtime decision
                     └─► M5 Rust SDK/builder/native FAP/Wasm
                          └─► M6 verification-only hardening/release
```

- No milestone starts implementation before every predecessor gate passes.
- Contract drafting for the next milestone may occur during predecessor verification, but code does not merge early.
- M6 adds no product features. A failed requirement returns to its owning milestone plan.
- No task commits without explicit user approval. Executors stage or show diffs, report the proposed commit message, and wait.
- Any firmware, bridge, dashboard, schema, or deployment change must update the documentation it affects in the same task.
- M6 may add tests, release automation, reports, packaging, and operational configuration, but no firmware/dashboard/bridge product feature first appears in M6.
- Every dependency needed by a milestone is implemented and gated in an earlier milestone; a milestone may not claim a requirement whose implementation is deferred.

## Implementation-Plan Completeness Contract

Every milestone executor must treat the following as mandatory:

1. Each task names exact create/modify/test paths. Existing files include verified line or symbol anchors; generated/future files name the owning generator or predecessor task.
2. Public messages, stored records, manifests, state machines, capabilities, and errors are specified before implementation. Nanopb bounds are explicit for every firmware string, byte field, and repeated field.
3. Every task uses red-green-refactor: add a failing regression/contract test, run the exact command and capture the expected failure, implement the smallest complete behavior, rerun the focused test, then run affected integration and milestone gates.
4. Every mutation defines authorization, exact-target confirmation when applicable, idempotency, cancellation, crash/power-loss behavior, audit emission, and rollback.
5. Every stream defines ordering, sequence reconciliation, credit/backpressure, disconnect behavior, bounded memory, truncation signaling, and resumption.
6. Every browser workflow defines keyboard/touch/screen-reader behavior, loading/empty/error/offline/reconnect states, and local-data retention.
7. Every security boundary has negative tests. A security defect is not complete until its regression test passes.
8. A task is not complete from a build alone: it needs the named unit/integration/E2E evidence. E2E labels require the real browser, transport, bridge where applicable, firmware, storage, and hardware.
9. Documentation and requirement evidence are updated in the same task. Claims cite current source paths and lines; planned paths are labeled as creates.
10. At each review gate, show diffs and results and request approval before any commit or external promotion.

## Requirement Traceability

| ID | Owner | Implementation proof | Release evidence |
|---|---|---|---|
| FR-1 | M1 | Pairing state machine and device confirmation | USB/BLE physical pair-control E2E |
| FR-2 | M1 | Transport interface plus USB/BLE adapters | Same session vectors over both physical transports |
| FR-3 | M1 | Installable service worker and offline application shell | Cold offline browser launch E2E |
| FR-4 | M1 | Loopback-only authenticated bridge | Supported-browser matrix and origin-attack tests |
| FR-5 | M1 | Typed device-status channel | Physical state mutation/status reconciliation E2E |
| FR-6 | M1 | Frame stream plus complete input event client | Timestamped USB/BLE screen-input E2E |
| FR-7 | M1 | Authenticated built-in/FAP lifecycle operations | Real legacy and integrated application E2E |
| FR-8 | M1 | Minimal structured app contract and legacy fallback; generalized in M3 | Both modes in one physical E2E |
| FR-9 | M1 | Ordered app logs/progress/warnings/results/artifacts | Saturated stream and artifact-digest E2E |
| FR-10 | M1/M2 | Safe disconnect plus transaction ownership cleanup | Disconnect during input, file mutation, and case capture |
| FR-11 | M2 | VFS over existing Storage API | `/int`/`/ext` compatibility and namespace E2E |
| FR-12 | M2 | Complete file operation contract and UI | Operation-by-operation 1,000-entry corpus E2E |
| FR-13 | M2 | Journaled mutation/recovery | 1,000 controlled interruption cycles |
| FR-14 | M2 | `/system`, `/config`, `/profiles`, `/apps`, `/scripts`, `/workloads`, `/cases`, `/evidence`, `/lessons`, `/exports`, `/int`, `/ext` | Namespace contract and migration tests |
| FR-15 | M2 | Case and ToolRun lifecycle | Create/select case before, during, and after a run |
| FR-16 | M2 | Raw/derived artifact separation | Digest invariance under preview/annotation |
| FR-17 | M2 | Complete evidence metadata schema | Required-field and canonicalization vectors |
| FR-18 | M2 | Append-only annotations | Raw-byte/digest invariance tests |
| FR-19 | M2 | Append-only audit chain | Creation/import/annotation/export/deletion chain tests |
| FR-20 | M2 | Deterministic signed portable export | Independent clean-host verification |
| FR-21 | M2 | Quarantining importer with complete error report | Missing/digest/schema/version corruption matrix |
| FR-22 | M2 | Content-addressed workspace snapshots/reset | Unrelated-case and personal-data isolation E2E |
| FR-23 | M3 | Local inventory and package-source state model | Six-state browser inventory E2E |
| FR-24 | M3 | Package verifier integrated before install/load | Integrity/target/API/signature/capability negative matrix |
| FR-25 | M3 | Transactional install/update/disable/remove/rollback | Power-loss and failed-health-check E2E |
| FR-26 | M3 | Ten audited hardware-family adapters | One real/safe fixture workflow per family |
| FR-27 | M3 | Catalog schema plus documentation/data gates | Catalog validator reports 100% completeness |
| FR-28 | M3 | Atomic field profile transaction | Apply/export/reset/import/rollback E2E |
| FR-29 | M3 | Theme/icon/font/menu/shortcut/status/lock/visibility editor | Preview, accessibility, invalid-pack, rollback E2E |
| FR-30 | M3 | Pre-activation asset verifier and known-good profile | Corrupt/incompatible asset recovery E2E |
| FR-31 | M3 | Signed lesson author/import/assign/export workflow | Educator/student physical workflow E2E |
| FR-32 | M3 | Lesson/classroom policy intersection | Tool/radio/bus/native/destructive denial matrix |
| FR-33 | M4 | Versioned JS project CRUD/upload/run/stop/rerun | Offline physical source-to-device E2E |
| FR-34 | M4 | Managed on-device mJS context and capability modules | Approved-module plus privilege-denial HIL |
| FR-35 | M4 | Ordered stdout/stderr/log/exception/stack/lifecycle/event/file stream | Saturation and disconnect reconciliation E2E |
| FR-36 | M4 | Bounded project manifest and lock schema | Cross-runtime valid/invalid fixtures |
| FR-37 | M4 | Authenticated installed-extension bundle channel | Physical device bundle-fetch E2E |
| FR-38 | M4 | Unique-origin sandbox, worker, CSP, typed broker | Browser escape/adversarial suite |
| FR-39 | M4 | Default-denied raw FFI and explicit trusted developer policy | Unsigned, expired, revoked, and stable-channel denial tests |
| FR-40 | M4 | Vendored immutable dependencies and cached IDE | WAN-disabled edit/run/serve E2E |
| FR-41 | M5 | Rust source/Cargo workspace with diagnostics/build events | Physical source-to-device E2E |
| FR-42 | M5 | Pinned network-denied OCI build worker | Escape/network/dependency-substitution tests |
| FR-43 | M5 | Constrained SDK/target/dependency/ABI/capability validator | Rejection matrix and ABI snapshot |
| FR-44 | M5 | Rust ELF integrated through existing FAP pipeline | Signed native FAP physical E2E |
| FR-45 | M4/M5 | Measured runtime decision then metered Wasm adapter | Trap/escape/resource HIL |
| FR-46 | M5 | Precompiled artifact admission verifier | Each signature/manifest/target/API/provenance/digest failure |
| FR-47 | M5 | Shared workload deploy/start/stop/output/crash/artifact protocol | Native and Wasm lifecycle E2E |
| FR-48 | M5 | Exact native trust/capability confirmation | Changed-target/replay/cancel denial tests |
| FR-49 | M5 | External signed provenance record | Full-field schema and independent verifier |
| FR-50 | M5 | Immutable source versions separated from artifacts | Rebuild/audit/history tests |
| FR-51 | M1 | Proof of possession and physical-device confirmation | Nearby-client attack test |
| FR-52 | M1 | Five-role policy engine | Complete role/capability matrix |
| FR-53 | M1/M3/M5 | Generic exact-target confirmation; M2–M3 callers integrated by M3 and native Rust callers by M5 | Operation-class replay/mutation E2E |
| FR-54 | M0/M3 | Signing hierarchy plus common content/update transaction manager | All five content types interrupted-update rollback E2E |
| FR-55 | M3 | Boot-accessible device-only recovery manager | Firmware/profile/index recovery with dashboard absent |
| FR-56 | M1/M2 | Shared audit service plus browser timeline | Every required event family and gap/tamper test |
| FR-57 | M1 | Device-side client revocation and session invalidation | Active/resumable reconnection denial |
| FR-58 | M1–M5/M6 | Hosted-service feature switch and local implementations | WAN-blocked full-core release E2E |
| FR-59–FR-64 | Post-V1 | Preserve protocol/storage extension points only | Excluded from V1 exit; no unsupported product claim |

## Non-Functional Release Matrix

| Area | Incremental owner | Exact release gate |
|---|---|---|
| Performance | M1–M5, aggregate M6 | Every SPEC p95 target measured on every supported platform; saturated streams lose zero unreported events |
| Reliability | M1–M5, aggregate M6 | 10,000 commands, 1,000 storage fault cycles, digest/audit correctness, update rollback, WAN-disabled core |
| Security/compliance | M0–M5, audit M6 | Required P-256/HKDF/AES-GCM suite, protected keys, signing hierarchy, redaction, encrypted host storage, radio policy, no admissibility claim |
| Capacity | M2–M5, aggregate M6 | 10,000 artifacts, 1,000 entries, 250 packages, 100 profiles, four bridge devices, bounded firmware queues |
| Accessibility/usability | M1–M5, study M6 | WCAG 2.2 AA; keyboard/touch/screen reader; exact-target warnings; ten-minute first-use path |
| Compatibility/maintainability | M0–M5, aggregate M6 | Current/previous protocol tests, upstream parity report, API snapshots, deterministic codegen, ownership and runbooks |

## Decision Gates

| Decision | Resolution task | Must resolve before | Blocking output |
|---|---|---|---|
| OQ-1 distribution model | M1 Task 1 | M1 completion | `ADR-0001-distribution-and-hosted-boundary.md` |
| OQ-2 direct/bridge platform matrix | M1 Task 2 | M1 design freeze | `ADR-0002-supported-client-transports.md` |
| OQ-3 upstream branch/sync cadence | M0 Task 2 | M0 baseline lock | `ADR-0003-upstream-baseline-and-sync.md` |
| OQ-4 signing-key control | M0 Task 8 | M0 exit | `ADR-0004-signing-authorities.md` |
| OQ-5 approved Rust dependencies | M5 Task 1 | Rust implementation | `ADR-0005-rust-dependency-policy.md` |
| OQ-6 Wasm runtime | M4 Task 8 | M4 exit | `ADR-0006-rust-wasm-runtime.md` |
| OQ-7 evidence extension/schema namespace | M2 Task 1 | M2 schema freeze | `ADR-0007-evidence-package-format.md` |
| OQ-8 curated V1 catalog | M3 Task 7 | M3 tool-adapter implementation | `ADR-0008-v1-tool-catalog.md` |
| OQ-9 hosted retention | M6 operational gate or before hosted beta | Any hosted beta | `ADR-0009-hosted-retention.md` |
| OQ-10 external validation | M6 release claims review | Public evidence claims | `ADR-0010-external-validation.md` |

An unresolved decision blocks only the work named above. It cannot be silently replaced with an executor preference.

## Cross-Milestone Quality Gates

Run at the end of every milestone after M0:

```bash
./fbt lint_all
./fbt f7
python3 tools/verify_baseline.py
python3 tools/verify_docs.py
pnpm --dir dashboard verify
cargo test --workspace --manifest-path bridge/Cargo.toml
python3 tools/hil/run_suite.py --suite firmware-units
python3 tools/hil/run_suite.py --suite milestone
```

Expected result: every command exits `0`; the HIL suite identifies physical device IDs and records firmware, dashboard, bridge, and protocol versions.

## Deferred Findings

This section starts empty. During execution, record only findings outside SPEC-1 or the active milestone. Every entry must name the discovering task, evidence path, effect, and proposed owner; it must not be implemented without a separate approved scope.

## Completion

The master plan is complete only when all seven milestone plan achievement ledgers read `PASS`, all requirements trace to passing evidence, and the stable release bundle can be reproduced and rolled back from documented inputs.
