# M2 Unified Files and Evidence Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use lore:execute to implement this plan task-by-task.
> **Scope guard:** Do ONLY what is listed here. If you discover adjacent issues, record them in the deferred-findings section and continue. Do NOT fix them.

**Goal:** Give field professionals, students, and educators one dependable filesystem and evidence workflow spanning the device, browser, and computer, with transactional capture and verifiable export.
**Architecture:** Place a versioned virtual filesystem over the firmware's existing `/int` and `/ext` storage, journal every mutation, and store evidence as immutable content-addressed objects with an append-only audit chain. Synchronize typed metadata through authenticated RPC and maintain a rebuildable local index in the Rust bridge.
**Tech Stack:** C/Furi storage, nanopb/protobuf, mbedTLS SHA-256, TypeScript/React, Rust/SQLite, Vitest, Playwright, property tests, fault injection, physical-device E2E.
**Practices:** Schema-first, crash consistency, immutable evidence, deterministic export, least privilege, security TDD, real-hardware E2E, approval-gated commits.
**Required skills:** `lore:test-driven-development`, `lore:security`, `lore:e2e-test-expert`, `lore:verification-before-completion`

---

## Achievement Ledger

| Achievement | Status before implementation | Required closing evidence |
|---|---|---|
| A2.1 Stable file and evidence contracts | Not started | ADR-0007, bounds, current/previous compatibility, and cross-language fixture tests pass. |
| A2.2 Crash-consistent VFS | Not started | Namespace/policy and 1,000-cycle journal fault matrix pass without acknowledged-data loss. |
| A2.3 Immutable evidence and auditable cases | Not started | Raw digest invariance, derived labeling, timestamp, and audit-chain tests pass. |
| A2.4 Browser workflows | Not started | Complete file/case/evidence/offline/reconnect UI flow passes with accessible states. |
| A2.5 Search, export, and workspace reset | Not started | 10,000-artifact index, independent export verification, and isolated reset pass. |
| A2.6 Reversible legacy migration | Not started | OFW and Momentum-derived fixtures inventory, convert, commit, and roll back byte-safely. |
| A2.7 Physical evidence workflow | Not started | Real browser-to-device capture/export/fault E2E passes without mocks. |

**Canonical task commands:** firmware tasks run `./fbt FIRMWARE_APP_SET=unit_tests` and `python3 tools/hil/run_suite.py --suite firmware-units`; dashboard tasks run `pnpm --dir dashboard verify`; bridge tasks run `cargo test --workspace --manifest-path bridge/Cargo.toml`; migration/export tools run `python3 -m unittest discover tools`; physical workflows run the exact HIL and Playwright commands named below. Record command, versions, device/media fixture IDs, exit code, and evidence digest before changing a ledger row.

## Achievement A2.1: Stable File and Evidence Contracts

### Task 1: Define namespaces, metadata, and transfer contracts

**Files:**
- Create: `assets/protobuf/poison_files.proto`
- Create: `assets/protobuf/poison_files.options`
- Create: `assets/protobuf/poison_evidence.proto`
- Create: `assets/protobuf/poison_evidence.options`
- Modify: `assets/protobuf/flipper.proto`
- Create: `schemas/poison/evidence-manifest.schema.json`
- Create: `docs/decisions/ADR-0007-evidence-package-format.md`
- Generate: `dashboard/src/generated/poison-files.ts` through the M0 protocol generator
- Generate: `bridge/src/generated/poison_files.rs` through the M0 protocol generator
- Test: `applications/debug/unit_tests/tests/storage/poison_file_contract_test.c`

**Contract:**

```protobuf
message EvidenceRecord {
  string evidence_id = 1;
  string case_id = 2;
  string source_app_id = 3;
  uint64 captured_at_unix_ms = 4;
  string device_id = 5;
  string firmware_version = 6;
  string content_sha256 = 7;
  uint64 content_length = 8;
  string media_type = 9;
  map<string, string> labels = 10;
  string previous_audit_sha256 = 11;
  string audit_sha256 = 12;
  string operator_id = 13;
  string tool_id = 14;
  string tool_version = 15;
  bytes canonical_parameters = 16;
  string source_path = 17;
}
```

**Steps:**

1. Resolve the evidence extension, media type, canonical JSON/protobuf representation, public schema namespace, archive layout, signing scope, and current/previous-version compatibility in ADR-0007.
2. Write failing round-trip and compatibility tests for paths, cursors, chunk offsets/digests, conflicts, quotas, Case, ToolRun, EvidenceArtifact, Annotation, AuditEvent, import report, and export manifest records.
3. Define `/system` read-only; `/config` internal with SD backup; `/profiles` as a stable logical namespace backed by versioned profile records under configuration storage with SD export/backup; `/apps`, `/scripts`, `/workloads`, `/cases`, `/evidence`, `/lessons`, and `/exports` on SD; and explicit `/int`/`/ext` compatibility views. Do not expose backing paths accidentally through logical APIs.
4. Add nanopb maxima for paths, names, tags, parameters, errors, chunks, page sizes, and event batches, then generate all bindings through M0 tooling and snapshot field numbers.
5. Run `python3 tools/protocol/check_generated.py`, the physical firmware-unit suite, `pnpm --dir dashboard test`, and `cargo test --manifest-path bridge/Cargo.toml` → Expected: identical contract fixtures pass in all three runtimes.
6. Stage diffs and request approval for proposed `feat(storage): define unified file and evidence contracts`.

## Achievement A2.2: Crash-Consistent Virtual Filesystem

### Task 2: Implement logical path resolution and access policy

**Files:**
- Create: `applications/services/poison_vfs/application.fam`
- Create: `applications/services/poison_vfs/poison_vfs.c`
- Create: `applications/services/poison_vfs/poison_vfs.h`
- Create: `applications/services/poison_vfs/poison_vfs_paths.c`
- Create: `applications/services/poison_vfs/poison_vfs_paths.h`
- Create: `applications/services/poison_vfs/poison_vfs_policy.c`
- Create: `applications/services/poison_vfs/poison_vfs_policy.h`
- Create: `applications/services/poison_vfs/poison_vfs_events.c`
- Create: `applications/services/poison_vfs/poison_vfs_events.h`
- Test: `applications/debug/unit_tests/tests/storage/poison_vfs_path_test.c`

**Steps:**

1. Write traversal, separator, Unicode, reserved-name, symlink-equivalent, cross-namespace, role, and quota tests.
2. Map logical namespaces explicitly to the existing Storage record and `/int`/`/ext` prefixes (`applications/services/storage/storage.h:15-27`); preserve current Storage service and RPC behavior for legacy applications.
3. Normalize a path exactly once and authorize the normalized form before opening any backing object.
4. Subscribe to existing mount/unmount/error events (`applications/services/storage/storage.h:48-73`), revoke affected handles, stop new mutations, and publish bounded logical storage events.
5. Extend the M1 diagnostic registry with bounded mount loss, recovery, journal replay/failure, and acknowledged-data-loss counters; add redaction and saturation regressions before emission.
6. Build and run the physical firmware-unit suite → Expected: valid paths resolve deterministically and every namespace escape/stale handle is rejected.

### Task 3: Add transaction journaling and recovery

**Files:**
- Create: `applications/services/poison_vfs/poison_vfs_journal.c`
- Create: `applications/services/poison_vfs/poison_vfs_journal.h`
- Create: `applications/services/poison_vfs/poison_vfs_recovery.c`
- Create: `applications/services/poison_vfs/poison_vfs_recovery.h`
- Test: `applications/debug/unit_tests/tests/storage/poison_vfs_journal_test.c`
- Test: `applications/debug/unit_tests/tests/storage/poison_vfs_recovery_test.c`

**Steps:**

1. Model `Prepared → DataSynced → MetadataCommitted → Complete` and write property tests that interrupt every transition.
2. Implement create, write/replace, mkdir, copy, rename/move, delete, and bounded batch operations with write-ahead records, per-operation idempotency keys, flush/digest barriers, and idempotent recovery.
3. Test corrupt/torn journal records, absent SD card, full media, hot removal, and repeated reboot recovery.
4. Run the storage unit suite and `python3 tools/hil/run_suite.py --suite storage-faults` → Expected: every operation is fully old or fully new; no orphan becomes visible.

**Achievement check:** Ten thousand randomized interrupted mutations recover to a valid namespace with no acknowledged write lost.

## Achievement A2.3: Immutable Evidence and Auditable Cases

### Task 4: Implement cases, tool runs, evidence capture, hashing, and audit chaining

**Files:**
- Create: `applications/services/poison_evidence/application.fam`
- Create: `applications/services/poison_evidence/poison_evidence.c`
- Create: `applications/services/poison_evidence/poison_evidence.h`
- Create: `applications/services/poison_evidence/poison_case.c`
- Create: `applications/services/poison_evidence/poison_case.h`
- Create: `applications/services/poison_evidence/poison_tool_run.c`
- Create: `applications/services/poison_evidence/poison_tool_run.h`
- Create: `applications/services/poison_evidence/poison_evidence_audit_adapter.c`
- Create: `applications/services/poison_evidence/poison_evidence_audit_adapter.h`
- Create: `applications/services/poison_evidence/poison_annotation.c`
- Create: `applications/services/poison_evidence/poison_annotation.h`
- Test: `applications/debug/unit_tests/tests/evidence/poison_evidence_test.c`
- Test: `applications/debug/unit_tests/tests/evidence/poison_audit_test.c`

**Steps:**

1. Write Case/ToolRun state tests for create/select before/during/after execution, participant/retention policy, start/stop/fail/cancel, late artifact attachment, and unauthorized cross-case access.
2. Write known-answer SHA-256, duplicate-content, interrupted-capture, raw/derived separation, metadata-validation, tamper, deletion/quarantine policy, timestamp-source, and audit-chain tests.
3. Stream captures into temporary objects, verify length and digest, atomically promote them, then append the M1 audit-service record in the same recoverable transaction.
4. Make evidence bytes immutable after promotion; previews are derived objects, notes/tags append annotation records, and corrections create a new record that references the superseded record.
5. Bind every record to case, optional run, operator, tool and version, canonical parameters, source path, device, firmware, timestamp source/quality, media type, size, and hash.
6. Build and run the physical firmware-unit suite → Expected: tampering or broken ancestry is reported and never silently repaired.

## Achievement A2.4: Browser File, Case, and Evidence Workflows

### Task 5: Implement resumable file transfer and browser management

**Files:**
- Create: `applications/services/rpc/rpc_poison_files.c`
- Create: `applications/services/rpc/rpc_poison_evidence.c`
- Modify: `applications/services/rpc/rpc.c`
- Create: `dashboard/src/files/FileBrowser.tsx`
- Create: `dashboard/src/files/FileTransferQueue.ts`
- Create: `dashboard/src/files/FileConflictDialog.tsx`
- Create: `dashboard/src/cases/CaseWorkspace.tsx`
- Create: `dashboard/src/evidence/EvidenceInspector.tsx`
- Create: `dashboard/src/evidence/EvidenceImport.tsx`
- Modify after M1 Task 12 creates it: `dashboard/src/audit/AuditTimeline.tsx`
- Create: `dashboard/src/offline/Database.ts`
- Create: `dashboard/src/offline/MutationQueue.ts`
- Test: `dashboard/src/files/FileTransferQueue.test.ts`
- Test: `dashboard/src/evidence/EvidenceInspector.test.tsx`
- Test: `dashboard/src/audit/AuditTimeline.test.tsx`
- Test: `dashboard/src/offline/MutationQueue.test.ts`

**Steps:**

1. Write tests for cursor pagination, search/sort determinism, 64-bit stat values, preview safety, chunk retry, checksum failure, reconnect resume, conflict policy, cancel, progress, quota, role denial, audit gaps, and offline mutation reconciliation.
2. Implement authenticated RPC handlers with bounded chunks and explicit operation IDs; retries must be idempotent.
3. Build browser flows to list, search, sort, stat, preview, read, upload, download, create directories, rename, copy, move, delete, checksum, create/select cases at every ToolRun stage, capture/attach/annotate evidence, import/export, and verify records.
4. Use IndexedDB for schemas, metadata index, source projects, queued annotations, and explicitly unacknowledged mutations; session keys and raw secrets never enter IndexedDB.
5. Merge every M1 and M2 audit family into a verified timeline with actor/action/resource/result/correlation filters and explicit chain-gap/tamper states.
6. Run `pnpm --dir dashboard verify`, physical firmware-unit tests, and bridge integration tests → Expected: failed transfers never appear complete and offline work never appears device-committed before acknowledgement.

## Achievement A2.5: Searchable Local Index and Portable Export

### Task 6: Build the bridge evidence index and deterministic exporter

**Files:**
- Create: `bridge/migrations/0001_evidence_index.sql`
- Create: `bridge/src/evidence/mod.rs`
- Create: `bridge/src/evidence/index.rs`
- Create: `bridge/src/evidence/export.rs`
- Create: `bridge/src/evidence/verify.rs`
- Create: `bridge/src/evidence/encryption.rs`
- Create: `bridge/src/evidence/import.rs`
- Create: `bridge/src/evidence/report.rs`
- Test: `bridge/tests/evidence_index.rs`
- Test: `bridge/tests/evidence_export.rs`
- Test: `bridge/tests/evidence_verify.rs`
- Test: `bridge/tests/evidence_import.rs`

**Steps:**

1. Test fresh indexing, incremental sync, interrupted sync, source deletion, database rebuild, hostile metadata, stable ordering, missing archive members, unexpected members, duplicate paths, zip/tar bombs, schema versions, and byte-for-byte repeat export.
2. Keep SQLite as a derived cache; rebuild it exclusively from device records and exported manifests.
3. Encrypt evidence and case data at rest when OS secure key storage is available; fail closed instead of silently writing sensitive plaintext when encrypted storage is required by policy.
4. Produce the ADR-0007 package containing raw artifacts, derived artifacts labeled separately, canonical metadata, annotations, audit history, per-file checksums, signed package manifest, human-readable report, and offline verification instructions.
5. Import into quarantine first; verify structure, bounds, schema version, signature scope, every object digest, every chain link, and cross-reference before accepting anything. Return all errors in one bounded report.
6. Emit audit events for import, export, annotation, and deletion/quarantine through the M1 service.
7. Extend M1 diagnostics with bounded evidence verification, chain-gap, import rejection, and index-rebuild counters that contain no case names, paths, payload bytes, or stable person identifiers.
8. Run `cargo test --workspace --manifest-path bridge/Cargo.toml` → Expected: independent re-index and verification produce identical results and every corrupt member is reported.

### Task 7: Implement isolated workspace snapshots and resets

**Files:**
- Create: `applications/services/poison_vfs/poison_workspace.c`
- Create: `applications/services/poison_vfs/poison_workspace.h`
- Create: `applications/services/rpc/rpc_poison_workspace.c`
- Create: `dashboard/src/cases/WorkspaceReset.tsx`
- Test: `applications/debug/unit_tests/tests/storage/poison_workspace_test.c`
- Test: `dashboard/src/cases/WorkspaceReset.test.tsx`

**Steps:**

1. Write tests for create snapshot, validate, preview reset, confirm, interrupt, resume, corrupt snapshot, quota, and attempts to reference paths outside the workspace.
2. Represent snapshots as immutable manifests over content-addressed objects and reset through the VFS journal.
3. Require the M1 exact-target confirmation contract and prove that reset cannot alter unrelated cases, evidence, profiles, or personal files.
4. Expose the primitive through authenticated RPC for later lesson packs while preserving a complete device-only reset path.
5. Run VFS, evidence, RPC, and dashboard tests → Expected: reset is repeatable and isolated.

## Achievement A2.6: Reversible Legacy Migration

### Task 8: Inventory existing OFW and Momentum-derived data without mutation

**Files:**
- Create: `applications/services/poison_vfs/poison_migration.c`
- Create: `applications/services/poison_vfs/poison_migration.h`
- Create: `schemas/poison/migration-manifest.schema.json`
- Create: `dashboard/src/migration/MigrationWizard.tsx`
- Test: `applications/debug/unit_tests/tests/storage/poison_migration_test.c`
- Test: `dashboard/src/migration/MigrationWizard.test.tsx`

**Steps:**

1. Inventory current `/int` and `/ext` paths read-only, classify standard OFW apps/scripts/captures/settings and verified Momentum settings/assets/app bundles, and record unknown formats without parsing them as trusted data.
2. Compute source digest, size, physical path, proposed logical reference, compatibility classification, required free space, and converter version in a signed migration manifest.
3. Back up internal storage and critical SD metadata before writing PoisonedOS state; refuse migration if backup verification or free-space checks fail.
4. Run fixture and physical read-only inventory tests → Expected: source bytes and metadata remain unchanged.

### Task 9: Create aliases, converters, commit, and rollback

**Files:**
- Create: `applications/services/poison_vfs/poison_compat_alias.c`
- Create: `applications/services/poison_vfs/poison_compat_alias.h`
- Create: `applications/services/poison_vfs/migrations/ofw_v1.c`
- Create: `applications/services/poison_vfs/migrations/momentum_v1.c`
- Create: `tools/migration/verify_manifest.py`
- Test: `applications/debug/unit_tests/tests/storage/poison_migration_commit_test.c`
- Test: `tools/migration/tests/test_verify_manifest.py`

**Steps:**

1. Create compatibility aliases for standard Flipper paths and reference existing apps, scripts, captures, and settings without rewriting raw files.
2. Import a legacy capture into a case only after exact-source/target confirmation; preserve original bytes/path/digest and create new metadata separately.
3. Treat Momentum state as an alternative implementation: convert only formats proven by fixtures from the inspected snapshot; leave unsupported state read-only with an explicit report.
4. Commit only versioned PoisonedOS metadata through the journal. On failure, remove only uncommitted metadata and restore prior settings from the verified backup.
5. Run firmware migration, host-manifest, and physical rollback suites → Expected: migration is repeatable/reversible and never deletes a legacy file.

## Achievement A2.7: Complete Physical Evidence Workflow

### Task 10: Add real-device file and evidence E2E coverage

**Files:**
- Create: `dashboard/e2e/files-evidence.spec.ts`
- Create: `tools/hil/suites/files_evidence.py`
- Create: `tools/hil/suites/storage_faults.py`
- Create: `docs/testing/m2-evidence-protocol.md`

**Steps:**

1. On physical hardware, create a case, capture the deterministic artifact from the M1 `poison_safe_sample` integrated application, disconnect mid-transfer, resume, verify, export, import on a clean bridge profile, and search it.
2. Exercise every FR-12 operation and snapshot/reset a classroom workspace while proving an unrelated case and personal file remain byte-identical.
3. Migrate standard OFW fixtures plus supported Momentum-derived fixtures, verify compatibility aliases, force migration failure, roll back, and compare all legacy bytes.
4. Repeat with SD removal, full media, power interruption at every journal boundary, dashboard termination, transport loss, and corrupted export bytes.
5. Generate and index a 10,000-artifact corpus and 1,000-entry directory, then verify search/list p95, completeness, pagination stability, storage bounds, and independent index rebuild.
6. Compare exported bytes, signatures, hashes, audit chain, and human report with an independent clean-host verifier.
7. Run `pnpm --dir dashboard e2e --grep @physical-evidence` and `python3 tools/hil/run_suite.py --suite files-evidence`.
8. Expected: acknowledged data survives; corrupted data is detected; migration reverses; browser, bridge, and device show one consistent record.

## M2 Exit Gate

```bash
./fbt lint_all
./fbt f7
./fbt FIRMWARE_APP_SET=unit_tests
python3 tools/hil/run_suite.py --suite firmware-units
pnpm --dir dashboard verify
cargo test --workspace --manifest-path bridge/Cargo.toml
python3 tools/migration/verify_manifest.py --check-fixtures
python3 tools/hil/run_suite.py --suite storage-faults
python3 tools/hil/run_suite.py --suite files-evidence
```

Expected: all exit `0`; evidence satisfies FR-11–FR-22 and FR-56, exports verify byte-for-byte, and fault injection loses no acknowledged transaction. Show diffs and request approval before commits.

## Deferred Findings

Record out-of-scope findings here with evidence, impact, and owner. Do not implement them during M2.
