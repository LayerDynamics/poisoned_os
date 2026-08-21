# M6 Hardening and Stable Release Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use lore:execute to implement this plan task-by-task.
> **Scope guard:** Do ONLY what is listed here. If you discover adjacent issues, record them in the deferred-findings section and continue. Do NOT fix them.

**Goal:** Convert the completed PoisonedOS feature set into a secure, reliable, accessible, reproducible, supportable V1 release with verified rollback and no dependency on hosted services.
**Architecture:** Freeze features and drive every release decision from machine-readable requirement evidence, adversarial security results, long-duration hardware runs, recovery exercises, accessibility/usability studies, and signed reproducible artifacts. Release through staged cohorts with automatic halt conditions and a proven rollback path.
**Tech Stack:** Existing firmware/dashboard/bridge/builder stacks, Python release tooling, protocol fuzzers, Cargo fuzzing, sanitizers, Playwright accessibility checks, SBOM/signing tools selected in M0, hardware-in-loop racks, CI release attestations.
**Practices:** No feature additions, risk-based verification, independent artifact reproduction, fail-closed release policy, zero critical/high security defects, real-system E2E, documented recovery drills, approval-gated external rollout and commits.
**Required skills:** `lore:execute`, `lore:test-driven-development`, `lore:testing`, `lore:security`, `lore:e2e-test-expert`, `lore:verification-before-completion`

---

## Achievement Ledger

| Achievement | Status before implementation | Required closing evidence |
|---|---|---|
| A6.1 Requirement/static closure | Not started | All normative IDs and pinned static/dependency/license/secret/generated-code gates pass. |
| A6.2 Adversarial security closure | Not started | Fuzz/threat/penetration/key-rotation matrix has zero open critical/high findings. |
| A6.3 Reliability/recovery/performance | Not started | Locked endurance/fault/capacity/latency/battery budgets pass on supported hardware. |
| A6.4 Accessibility/usability | Not started | Automated WCAG checks and professional/student/educator/first-use studies pass. |
| A6.5 Update/local-only verification | Not started | Independently built release rolls back correctly and full core works with WAN blocked. |
| A6.6 Operations/support readiness | Not started | Current runbooks/drills, retention decision, redaction, and support packaging pass. |
| A6.7 Stable candidate/release | Not started | ADR-0010 claims audit and independently reproduced candidate receive explicit promotion approvals. |

**Canonical task commands:** every M0–M5 exit gate is rerun from clean pinned inputs; M6 tools use their exact commands named below; dashboard, Rust, firmware, and HIL stacks use the same canonical commands as their owning milestones. Evidence is invalid unless it records source/artifact/config digests, versions, exit code, reviewer, timestamp, and physical IDs where applicable.

## Achievement A6.1: Machine-Verifiable Requirement Closure

### Task 1: Build the requirement and evidence ledger

**Files:**
- Create: `docs/release/v1-requirement-ledger.md`
- Create: `docs/release/v1-supported-platforms.md`
- Create: `docs/release/v1-known-limitations.md`
- Create: `tools/release/verify_spec.py`
- Create: `tools/release/requirement_evidence.schema.json`
- Create: `artifacts/release-evidence/.gitkeep`
- Test: `tools/release/tests/test_verify_spec.py`

**Steps:**

1. Parse FR-1–FR-58 and every normative performance, reliability, security, privacy, accessibility, compatibility, maintainability, and operational release gate from SPEC-1 into stable IDs.
2. Require each item to name owning milestone, automated command, evidence path, tested versions, physical-device IDs where applicable, result, reviewer, and timestamp.
3. Write failing verifier tests for missing IDs, duplicate evidence, stale versions, skipped commands, synthetic hardware identifiers, expired security evidence, and failed dependencies.
4. Populate the ledger only from current M0–M5 outputs; a missing proof remains `FAIL`, never a prose assertion.
5. Run `python3 tools/release/verify_spec.py --spec docs/specs/SPEC-1-poisonedos-for-the-flipper-zero.md --evidence artifacts/release-evidence` → Expected: an actionable failure list before the remaining M6 work.
6. Stage diffs and request approval for proposed `test(release): make v1 requirements machine-verifiable`.

### Task 2: Run static, dependency, license, secret, and generated-code gates

**Files:**
- Create: `tools/release/run_static_gates.py`
- Create: `config/release-static-gates.json`
- Create: `docs/release/v1-static-analysis-report.md`
- Test: `tools/release/tests/test_run_static_gates.py`

**Steps:**

1. Register exact pinned commands for firmware lint/build warnings, Python lint/type/test, TypeScript lint/type/test/build, Rust formatting/clippy/test/audit, protobuf/C-TypeScript-Rust generated drift, dependency/license/SBOM verification, secret scanning, and documentation links/code-fence validation.
2. Store tool name/version/config digest, included/excluded paths, command, exit status, findings, suppressions, owner, and evidence digest for every gate; an unavailable or unpinned tool is a failed gate.
3. Permit a suppression only with finding ID, evidence-based rationale, scope, reviewer, expiration, and linked regression/issue; critical/high security results cannot be suppressed for stable release.
4. Test missing tool, partial scan, stale result, generated drift, unknown license, vulnerable dependency, committed secret fixture, expired suppression, and changed configuration.
5. Run `python3 tools/release/run_static_gates.py --profile stable` → Expected: all configured source and generated outputs are scanned with zero unowned or expired finding.

## Achievement A6.2: Adversarial Security Closure

### Task 3: Fuzz every untrusted boundary

**Files:**
- Create: `tests/fuzz/protocol/Cargo.toml`
- Create: `tests/fuzz/protocol/fuzz_targets/session_envelope.rs`
- Create: `tests/fuzz/packages/Cargo.toml`
- Create: `tests/fuzz/packages/fuzz_targets/package_manifest.rs`
- Create: `tests/fuzz/evidence/Cargo.toml`
- Create: `tests/fuzz/evidence/fuzz_targets/evidence_manifest.rs`
- Create: `tests/fuzz/wasm/Cargo.toml`
- Create: `tests/fuzz/wasm/fuzz_targets/module_loader.rs`
- Create: `tools/security/run_fuzz_matrix.py`
- Create: `docs/security/v1-fuzzing.md`

**Steps:**

1. Seed corpora with valid, truncated, oversized, duplicated, reordered, cross-version, and prior regression fixtures.
2. Fuzz RPC/session decoding, pairing states, file paths, journal recovery, evidence manifests, package archives, JS manifests/messages, Wasm modules/imports, update manifests, and bridge HTTP/WebSocket parsing.
3. Run host fuzzing with sanitizers and bounded on-device replay of minimized cases.
4. Convert every discovered defect into a regression test in its owning milestone before fixing it; rerun that milestone's full gate.
5. Run `python3 tools/security/run_fuzz_matrix.py --profile release` → Expected: no crash, hang, memory error, policy bypass, or untriaged unique finding at the release duration threshold.

### Task 4: Execute the V1 threat-model and penetration matrix

**Files:**
- Create: `docs/security/v1-threat-model.md`
- Create: `docs/security/v1-security-test-matrix.md`
- Create: `docs/security/key-rotation-and-revocation.md`
- Create: `docs/security/incident-response.md`
- Create: `tools/security/run_adversarial_suite.py`
- Create: `tools/hil/suites/security_adversarial.py`
- Test: `tools/security/tests/test_security_matrix.py`

**Steps:**

1. Re-evaluate assets, trust boundaries, attacker access, student/instructor separation, local bridge, hosted builder, signing, update, evidence, JS, native Rust, Wasm, and physical-loss cases against the implemented system.
2. Test replay, origin attacks, malicious USB/BLE peers, downgrade, signature bypass, key revocation, capability escalation, evidence tampering, dependency substitution, builder escape, JS browser/device escape, Wasm escape, native API misuse, secret leakage, and physical revocation.
3. Exercise release, package, and pairing key rotation and revocation from initial trigger through distribution and verified denial.
4. Require independent review of all critical boundaries and close findings through regression-tested owning-milestone changes.
5. Run `python3 tools/security/run_adversarial_suite.py --release-candidate` and `python3 tools/hil/run_suite.py --suite security-adversarial` → Expected: zero open critical/high findings and no unowned medium finding.

**Achievement check:** A release candidate cannot be marked secure solely from static scans; every critical trust boundary has executable adversarial evidence.

## Achievement A6.3: Reliability, Recovery, and Performance

### Task 5: Prove endurance and fault recovery

**Files:**
- Create: `tools/hil/suites/v1_endurance.py`
- Create: `tools/hil/suites/v1_fault_matrix.py`
- Create: `tools/hil/suites/v1_recovery.py`
- Create: `docs/testing/v1-endurance-protocol.md`
- Create: `docs/runbooks/storage-corruption-recovery.md`
- Create: `docs/runbooks/device-recovery.md`
- Create: `docs/runbooks/evidence-recovery.md`

**Steps:**

1. Before candidate testing, lock the endurance protocol to 24 continuous hours over USB and 24 continuous hours over BLE on each supported hardware revision, plus a seven-day idle/reconnect/clock rollover run on at least two devices; changing these durations requires a reviewed release-risk amendment and fresh evidence.
2. During each active run, exercise concurrent screen streaming, input, file transfer, evidence capture, app sessions, JavaScript, Wasm, and supported native workloads with fixed reproducible operation rates and fixture digests.
3. Inject USB/BLE loss, bridge/browser restart, SD removal, media full, torn writes, low battery, device reset, malformed traffic, workload exhaustion, and update interruption at controlled boundaries.
4. Verify reconnect, idempotency, journal replay, evidence integrity, workload cleanup, update rollback, and device-only recovery after every fault.
5. Execute each recovery runbook from a clean operator following only the document; update the same runbook for every discovered ambiguity.
6. Run `python3 tools/hil/run_suite.py --suite v1-endurance --release-duration`, `python3 tools/hil/run_suite.py --suite v1-fault-matrix`, and `python3 tools/hil/run_suite.py --suite v1-recovery` → Expected: all locked durations and SPEC reliability thresholds pass with no unexplained reset or acknowledged-data loss.

### Task 6: Verify latency, throughput, resource, and battery budgets

**Files:**
- Create: `tools/hil/suites/v1_performance.py`
- Create: `tools/release/compare_budgets.py`
- Create: `docs/release/v1-performance-report.md`
- Test: `tools/release/tests/test_compare_budgets.py`

**Steps:**

1. Measure dashboard cold start, pairing, command round trip, input-to-frame feedback, 1,000-entry directory listing, transfer throughput, evidence finalization, 10,000-artifact search, app start, 100 KiB JS start/stop, Wasm start/stop, native app start, first Rust build feedback, output saturation, and update/rollback.
2. Exercise 250 installed packages, 100 profiles, four concurrent bridge devices, bounded RPC queues/credit flow, and the build queue isolation/scaling policy; record correctness and responsiveness at each required capacity.
3. Record flash, static RAM, peak heap, task/thread counts, storage growth, bridge/dashboard memory, builder resources, and battery effect under idle and representative field use.
4. Run 10,000 nominal USB and BLE control commands and reconcile every streamed sequence number so no successful-command or silent-output-loss threshold is inferred from a short run.
5. Compare distributions and worst supported-platform results with SPEC budgets; averages cannot hide percentile or platform failures.
6. Run `python3 tools/hil/run_suite.py --suite v1-performance` and `python3 tools/release/compare_budgets.py artifacts/release-evidence/performance` → Expected: every required budget passes or release remains blocked.

## Achievement A6.4: Accessibility and Usability Closure

### Task 7: Verify browser, device, professional, and classroom usability

**Files:**
- Create: `dashboard/e2e/accessibility.spec.ts`
- Create: `dashboard/e2e/error-recovery.spec.ts`
- Create: `docs/testing/v1-accessibility-protocol.md`
- Create: `docs/testing/v1-usability-protocol.md`
- Create: `docs/release/v1-accessibility-report.md`
- Create: `docs/release/v1-usability-report.md`

**Steps:**

1. Automate keyboard-only navigation, focus order/visibility, names/roles, contrast, zoom/reflow, reduced motion, status announcements, error association, and timeout recovery across every primary dashboard workflow.
2. Test device-only pairing, files/evidence, profiles, lessons, workload stop/recovery, revocation, and corruption recovery with no browser available.
3. Freeze recruitment and procedure before testing: at least 20 field professionals, 20 students, 10 educators, and 12 users who actively use relevant assistive technology; a participant may count in more than one cohort only when each role-specific protocol is run and reported separately. Cover every supported client class from ADR-0002 across the combined sample.
4. Use real hardware and production-equivalent builds to test pairing/control, safe sample, case/evidence export, tool use, classroom assignment/reset, error recovery, and device-only recovery. The first-time path is pairing → case creation → safe sample → verified export with no external documentation.
5. Record task success, errors, recovery, assistance, duration, platform, input/accessibility mode, and qualitative blockers without collecting unnecessary sensitive evidence content.
6. Require pair-to-first-control ≥95% without support, safe-sample completion ≥90% within 10 minutes, browser-managed field sessions ≥80% without CLI fallback, 100% valid evidence exports, and zero WCAG 2.2 AA violations or critical accessibility blockers in primary flows.
7. Fix failed launch criteria through their owning milestone with regression tests; recruit fresh participants for materially changed first-use flows and rerun the complete affected study slice.
8. Run `pnpm --dir dashboard e2e --grep @accessibility` and `pnpm --dir dashboard e2e --grep @error-recovery` → Expected: automated checks and the locked participant thresholds pass.

## Achievement A6.5: Signed Updates, Rollback, and Local-Only Operation

### Task 8: Package and independently verify the existing M3 update pipeline

**Files:**
- Create: `schemas/poison/release-manifest.schema.json`
- Create: `tools/release/build_release.py`
- Create: `tools/release/sign_release.py`
- Create: `tools/release/verify_release.py`
- Create: `tools/release/promote_release.py`
- Create: `tools/release/package_platforms.py`
- Test: `tools/release/tests/test_reproducible_release.py`
- Test: `tools/release/tests/test_package_platforms.py`
- Test: `applications/debug/unit_tests/tests/update/poison_update_test.c`

**Steps:**

1. Define a release-level signed manifest that composes the M3 firmware/content manifests and covers firmware, resources, dashboard, bridge, builder/SDK compatibility, schemas, digests, channel, minimum/maximum versions, rollback target, and revocations without creating a second device update state machine.
2. Re-run the M3 tamper, wrong-target, downgrade, expired/revoked-signer, partial-download, every-activation-boundary interruption, failed-health-check, rollback, and network-disabled recovery cases against assembled release artifacts.
3. Package the firmware/update/resources bundle, installable offline dashboard/static self-host bundle, bridge installer/archive for every supported OS/architecture row in ADR-0002, local-builder OCI image, Rust SDK/toolchain archive, schemas, licenses/notices, SBOM, provenance, signatures, and offline verification utility. A supported row without a produced and install-tested artifact blocks release.
4. Build the full release twice from pinned clean inputs on independent workers and require byte-identical artifacts or documented signed non-deterministic metadata outside artifact digests.
5. Install every platform package on a clean supported baseline, reject wrong-OS/architecture/version artifacts, and verify uninstall/upgrade preserves user-owned local data according to retention policy.
6. Implement staged promotion metadata and automatic halt signals; promotion changes external release state and requires separate explicit user approval at every channel transition.
7. Run package/SBOM/license/provenance/signature/update tests → Expected: every supported platform installs locally and interrupted or unhealthy device updates boot the previous verified release.

### Task 9: Prove complete local-only and hosted-service-disabled behavior

**Files:**
- Create: `tools/hil/suites/local_only.py`
- Create: `dashboard/e2e/local-only.spec.ts`
- Create: `docs/runbooks/local-only-operation.md`
- Create: `docs/runbooks/self-hosted-dashboard.md`

**Steps:**

1. Block all external network routes and disable hosted service configuration before launching any component.
2. Pair/control the device, manage files, capture/export/verify evidence, run installed apps, run installed JavaScript/Wasm/native Rust artifacts, customize a profile, and complete an installed lesson.
3. Build Rust projects through the local builder using previously vendored dependencies and install the verified artifacts.
4. Serve the dashboard from its static bundle and from the local bridge; verify no hidden external request or hosted credential requirement.
5. Run `pnpm --dir dashboard e2e --grep @local-only` and `python3 tools/hil/run_suite.py --suite local-only` → Expected: FR-58 passes and network capture shows no attempted external dependency.

## Achievement A6.6: Operations, Compatibility, and Support Readiness

### Task 10: Complete operational documentation, hosted-retention decision, and drills

**Files:**
- Create: `docs/compatibility-policy.md`
- Create: `docs/data-retention.md`
- Create: `docs/runbooks/release.md`
- Create: `docs/runbooks/rollback.md`
- Create: `docs/runbooks/key-compromise.md`
- Create: `docs/runbooks/builder-isolation.md`
- Create: `docs/runbooks/support-triage.md`
- Create: `docs/release/v1-maintainers.md`
- Create: `docs/decisions/ADR-0009-hosted-retention.md`
- Create: `tools/release/verify_runbooks.py`
- Test: `tools/release/tests/test_verify_runbooks.py`

**Steps:**

1. Document supported firmware API, protocol, package, JavaScript runtime, Rust SDK/ABI, Wasm host API, dashboard, bridge, builder, and data/export compatibility windows.
2. Document retention/export/deletion behavior for local device data, browser data, bridge index, builder inputs/logs, optional hosted data, and support bundles. ADR-0009 must either fix hosted defaults, maximums, user/organization controls, backup deletion, legal holds, export, account deletion, and verification, or explicitly exclude hosted beta from V1 and define the evidence required to reopen that scope; it may not leave the question unanswered.
3. Name accountable firmware, dashboard, security, and release maintainers plus backup/escalation paths before release.
4. Conduct release, rollback, key-compromise, builder-isolation, corrupt-storage, and support-triage tabletop plus live drills.
5. Run `python3 tools/release/verify_runbooks.py` → Expected: current commands, owners, prerequisites, decision points, verification, and recovery are present and successfully exercised.

### Task 11: Verify and package the existing privacy-preserving diagnostics without adding signals

**Files:**
- Create: `tools/release/evaluate_rollout_health.py`
- Create: `tools/security/verify_redaction.py`
- Create: `docs/privacy/diagnostics.md`
- Create: `dashboard/e2e/support-bundle.spec.ts`
- Test existing M1–M5 coverage: `applications/debug/unit_tests/tests/diagnostics/poison_diagnostics_test.c`
- Test existing M1–M5 coverage: `bridge/tests/support_bundle.rs`
- Test: `tools/release/tests/test_evaluate_rollout_health.py`
- Test: `tools/security/tests/test_verify_redaction.py`

**Steps:**

1. Audit the M1–M5 diagnostic registry for transport, evidence integrity, filesystem recovery, update/rollback, builder queue/failure, workload crash/limit, package revocation, and support categories. A missing signal fails M6 and returns to its owning milestone; M6 does not add the product signal.
2. Exclude secrets, raw credential captures, case/evidence contents, private keys, source files, and stable personal identifiers from ordinary logs, analytics, and default support bundles.
3. Verify the M1 preview/selective-export flow and package its support bundle with versions, health counters, redacted errors, consent record, and integrity manifest; local diagnostics work with telemetry disabled.
4. Compute launch metrics and rollout halt triggers from versioned evidence without silently uploading data; hosted reporting is opt-in and follows the documented retention/deletion policy.
5. Test redaction with generated secret/case fixtures, malicious filenames/metadata, crash paths, truncation, opt-out, local-only operation, and bundle tampering.
6. Run `python3 tools/security/verify_redaction.py`, `pnpm --dir dashboard e2e --grep @support-bundle`, all existing diagnostic/support tests, and `python3 tools/release/evaluate_rollout_health.py --candidate` → Expected: required pre-existing health signals remain actionable and prohibited content never appears in logs or bundles.

## Achievement A6.7: Stable Candidate and Controlled Release

### Task 12: Decide external validation claims

**Files:**
- Create: `docs/decisions/ADR-0010-external-validation.md`
- Create: `tools/release/verify_claims.py`
- Test: `tools/release/tests/test_verify_claims.py`

**Steps:**

1. Inventory public-facing claims in release notes, dashboard copy, evidence reports, website/package metadata, lesson content, and support material.
2. Decide whether any external laboratory/security/forensic validation is commissioned for V1, including scope, provider independence, artifact version, limitations, publication, renewal, and owner.
3. Regardless of decision, prohibit “forensic certification,” “admissible evidence,” “court-ready,” or equivalent claims unless the exact released artifact is covered by cited independent evidence.
4. Test disallowed phrases, uncited validation badges, expired/wrong-version reports, and permitted precise integrity-language fixtures.
5. Run `python3 tools/release/verify_claims.py` → Expected: all release claims match ADR-0010 and SPEC non-goals.

### Task 13: Assemble, independently verify, and approve V1

**Files:**
- Create: `docs/release/v1-release-checklist.md`
- Create: `docs/release/v1-release-notes.md`
- Create: `docs/release/v1-final-report.md`
- Create: `tools/release/assemble_evidence.py`
- Create: `tools/release/verify_candidate.py`
- Test: `tools/release/tests/test_verify_candidate.py`

**Steps:**

1. Freeze the candidate inputs and record upstream base, source revision, dependency lockfiles, toolchains, signing key IDs, protocol/API versions, supported platforms, and reproducible build instructions.
2. Run every M0–M5 exit gate from clean state, then all M6 suites, without suppressions, mock transports, stale artifacts, or reused pass records.
3. Have an independent worker rebuild and verify signatures, SBOM, licenses, provenance, package inventory, release manifest, and evidence ledger.
4. Confirm zero open critical/high defects, no unowned release risk, all 58 V1 MUST requirements pass, and every applicable NFR/release gate has current evidence.
5. Exercise migration/install from the locked OFW baseline, update from the latest signed PoisonedOS beta candidate, fresh installation, failed activation, rollback, local-only operation, and revocation on physical devices; the public compatibility policy must name exactly which predecessor paths are supported.
6. Present the candidate report, exact artifacts, residual limitations, staged rollout plan, halt thresholds, and rollback target; request explicit approval before signing final promotion metadata or changing any external release channel.
7. Configure hard halts for any evidence digest/audit corruption, confirmed signature/pairing/sandbox/capability bypass, command success below 99.5%, crash-free sessions below 99%, update failure above 0.5%, any unrecoverable update, a p95 transport latency target exceeded by 50% for two consecutive cohorts, or a critical accessibility blocker.
8. After approval, promote through internal, developer, and beta channels one transition at a time, then advance the stable channel through 1%, 10%, 25%, 50%, and 100% cohorts. Verify daily health during staged rollout, enforce automatic halt conditions, and request explicit approval before every channel transition and stable-cohort increase.

## M6 Exit Gate

```bash
./fbt lint_all
./fbt f7
./fbt FIRMWARE_APP_SET=unit_tests
python3 tools/hil/run_suite.py --suite firmware-units
python3 tools/verify_baseline.py
python3 tools/verify_docs.py
python3 tools/release/run_static_gates.py --profile stable
python3 tools/catalog/validate_catalog.py
pnpm --dir dashboard verify
cargo test --workspace --manifest-path bridge/Cargo.toml
cargo test --workspace --manifest-path rust-sdk/Cargo.toml
cargo test --workspace --manifest-path builder/Cargo.toml
python3 tools/security/run_fuzz_matrix.py --profile release
python3 tools/security/run_adversarial_suite.py --release-candidate
python3 tools/hil/run_suite.py --suite v1-endurance --release-duration
python3 tools/hil/run_suite.py --suite v1-fault-matrix
python3 tools/hil/run_suite.py --suite v1-performance
python3 tools/hil/run_suite.py --suite v1-recovery
python3 tools/hil/run_suite.py --suite local-only
python3 tools/release/verify_runbooks.py
python3 tools/release/verify_claims.py
python3 tools/release/verify_spec.py --spec docs/specs/SPEC-1-poisonedos-for-the-flipper-zero.md --evidence artifacts/release-evidence
python3 tools/release/verify_candidate.py --evidence artifacts/release-evidence
```

Expected: all exit `0`; all 58 V1 MUST requirements and every applicable NFR/release gate pass, all product services were completed in M0–M5, artifacts reproduce independently, local-only operation is complete, rollback is proven through the M3 updater, public claims match ADR-0010, and zero critical/high defects remain. Show diffs and request approval before commits and before any external rollout.

## Deferred Findings

Record out-of-scope findings here with evidence, impact, and owner. Do not implement them during M6.
