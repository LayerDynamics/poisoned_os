# M0 Baseline and Governance Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use lore:execute to implement this plan task-by-task.
> **Scope guard:** Do ONLY what is listed here. If you discover adjacent issues, record them in the deferred-findings section and continue. Do NOT fix them.

**Goal:** Establish a buildable, reproducible, legally complete PoisonedOS repository with measured firmware behavior and a functioning quality pipeline.
**Architecture:** Treat the local OFW snapshot as a read-only provenance source and the workspace root as the new PoisonedOS repository. Restore missing build metadata without overwriting product files, vendor the already-present dependency sources deliberately, and lock every imported component by digest.
**Tech Stack:** Git, FBT/SCons, Python 3, C/C++, GitHub Actions-compatible workflows, SPDX/CycloneDX SBOM, two physical Flipper Zero recovery devices.
**Practices:** Evidence-first, reproducible builds, TDD for verification scripts, approval-gated commits.
**Required skills:** `lore:test-driven-development`, `lore:security`, `lore:verification-before-completion`, `lore:verify-before-documenting`

---

## Achievement Ledger

| Achievement | Status before implementation | Required closing evidence |
|---|---|---|
| A0.1 Repository and baseline identity | Not started | Baseline/upstream verifiers pass and repository status is captured without a commit. |
| A0.2 Build and license restoration | Not started | Root build entrypoint works; SPDX/CycloneDX output has zero unknown licenses. |
| A0.3 Reproducible build and quality gates | Not started | Two clean builds match and all local/CI commands pass from pinned tools. |
| A0.4 Security and hardware readiness | Not started | Signing/revocation vectors pass and both physical HIL roles recover. |
| A0.5 Shared protocol and compatibility tooling | Not started | C/TypeScript/Rust generation is deterministic and official API/parity checks pass. |

**Canonical task commands:** Python verifier tests use `python3 -m unittest discover tools/tests`; firmware checks use `./fbt lint_all`, `./fbt f7`, and `./fbt FIRMWARE_APP_SET=unit_tests`; physical unit execution uses `python3 tools/hil/run_suite.py --suite firmware-units`. A task adds its named focused command when it introduces a distinct executable or HIL suite. Record command, exit code, tool/input versions, and evidence digest before changing its ledger row.

## Achievement A0.1: Repository and Baseline Identity

### Task 1: Record the immutable baseline contract

**Files:**
- Create: `provenance/baseline.lock.json`
- Create: `provenance/README.md`
- Create: `tools/verify_baseline.py`
- Test: `tools/tests/test_verify_baseline.py`

**Steps:**

**Step 1: Write the failing test**

Test that `baseline.lock.json` names commit `a55e39395ff31bd5fdf3929c70720a7fb76e5968`, records the source mode and SHA-256 for every materialized upstream path, records every intentional PoisonedOS-only path, captures all twelve upstream dependency pins, and rejects a changed, missing, extra, or mode-changed path.

**Step 2: Run to verify it fails**

`python3 -m unittest tools.tests.test_verify_baseline` → Expected: `FAIL` because the lock and verifier do not exist.

**Step 3: Implement the verifier**

Implement deterministic JSON loading, required-field validation, SHA-256 streaming, missing/extra path reporting, and nonzero exit status on any mismatch. The lock schema is:

```json
{
  "schema": "poison.baseline/v1",
  "upstream": "flipperdevices/flipperzero-firmware",
  "commit": "a55e39395ff31bd5fdf3929c70720a7fb76e5968",
  "sourcePath": "do_not_include/flipperzero-firmware",
  "dependencyMode": "resolved-by-ADR-0003",
  "upstreamTreeSha256": "computed-from-sorted-path-mode-digest-records",
  "files": [],
  "dependencies": []
}
```

**Step 4: Verify**

`python3 -m unittest tools.tests.test_verify_baseline && python3 tools/verify_baseline.py` → Expected: `OK` and `baseline verification passed`.

**Step 5: Review gate**

Run `git diff --check` after Git initialization, show the diff, propose `chore: lock official firmware baseline`, and request approval before committing.

### Task 2: Decide the upstream and dependency strategy

**Files:**
- Create: `docs/decisions/ADR-0003-upstream-baseline-and-sync.md`
- Create: `provenance/upstream-paths.json`
- Create: `tools/compare_upstream.py`
- Test: `tools/tests/test_compare_upstream.py`

**Steps:**

1. Add fixtures covering identical upstream files, intentional PoisonedOS modifications, upstream-only paths, PoisonedOS-only paths, changed modes, and dependency gitlinks.
2. Compare the workspace against `git -C do_not_include/flipperzero-firmware ls-tree -r HEAD`, using the checked-out file bytes for content digests and never flattening comparison repositories into product provenance.
3. Record the official `dev` snapshot, update cadence, merge/rebase policy, security-patch intake, conflict ownership, API compatibility policy, and either fully vendored dependency directories or restored submodules. Mixing untracked vendor directories with submodule assumptions is forbidden.
4. Generate `provenance/upstream-paths.json` with one classification per path: `identical`, `poison-modified`, `poison-added`, `upstream-omitted`, or `dependency`.
5. Run `python3 -m unittest tools.tests.test_compare_upstream` and `python3 tools/compare_upstream.py --check` → Expected: fixtures pass and every current product path is classified.
6. Show the ADR and tree report and request approval before locking the baseline choice.

### Task 3: Initialize repository metadata without committing

**Files:**
- Create: `.git/` through `git init`
- Modify: `.gitignore:1`

**Steps:**

1. Run `git init -b main` → Expected: initialized empty repository.
2. Add explicit ignores for `do_not_include/`, build output, generated keys, local HIL inventory, dashboard dependencies, Rust targets, and editor state.
3. Run `git check-ignore do_not_include/flipperzero-firmware/.git/HEAD` → Expected: ignored.
4. Run `git status --short` and save its output in the execution evidence; no commit occurs.

**Achievement check:** Repository recognizes all product files while excluding comparison clones, secrets, and generated output.

## Achievement A0.2: Build and License Restoration

### Task 4: Restore missing root build and policy files

**Files:**
- Create: `.clang-format`
- Create: `.clangd`
- Create: `.editorconfig`
- Create: `.gitattributes`
- Create or intentionally replace through ADR-0003: `.gitmodules`
- Create: `.pvsconfig`
- Create: `.pvsoptions`
- Create: `CODE_OF_CONDUCT.md`
- Create: `CODING_STYLE.md`
- Create: `CONTRIBUTING.md`
- Create: `LICENSE`
- Create: `SConstruct`
- Create: `fbt`
- Create: `fbt.cmd`
- Create: `fbt_options.py`
- Create: `firmware.scons`
- Create: `tsconfig.json`

**Steps:**

1. Verify every source file and mode against the OFW commit recorded in `provenance/baseline.lock.json`.
2. Add each missing file with exact content from the locked source using `apply_patch`; do not overwrite `README.md` or PoisonedOS documentation. Handle `.gitmodules` exactly as ADR-0003 selects.
3. Preserve GPLv3 text exactly in `LICENSE` and document the derivative status in `provenance/README.md`.
4. Run `python3 tools/verify_baseline.py` → Expected: PASS.
5. Run `./fbt --help` → Expected: exit `0` and print FBT targets.

**Achievement check:** The root has a complete build entrypoint and licensing surface without deleting or replacing user-created files.

### Task 5: Inventory and license every component

**Files:**
- Create: `provenance/components.json`
- Create: `provenance/licenses.json`
- Create: `tools/generate_sbom.py`
- Test: `tools/tests/test_generate_sbom.py`
- Create: `artifacts/sbom/.gitkeep`

**Steps:**

1. Write tests requiring component path, version/commit, SHA-256, license ID, source URL, and modification status.
2. Implement deterministic SPDX JSON and CycloneDX JSON generation.
3. Run `python3 -m unittest tools.tests.test_generate_sbom` → Expected: PASS.
4. Run `python3 tools/generate_sbom.py --check` → Expected: PASS with zero unknown licenses.
5. Record any component with ambiguous licensing as a release blocker; do not assign a guessed license.

**Achievement check:** SBOMs reproduce byte-for-byte and every distributed component has verified provenance and license data.

## Achievement A0.3: Reproducible Build and Quality Gates

### Task 6: Pin the build toolchain and establish baseline firmware builds

**Files:**
- Modify after Task 4 creates it: `fbt_options.py`
- Modify: `scripts/toolchain/fbtenv.sh`
- Create: `toolchains/manifest.lock.json`
- Create: `tools/verify_toolchain.py`
- Create: `docs/development/build-baseline.md`
- Create: `tools/check_reproducible_build.py`
- Test: `tools/tests/test_check_reproducible_build.py`

**Steps:**

1. Preserve OFW-compatible defaults except set product origin and artifact suffix to PoisonedOS values documented in the spec.
2. Record the exact FBT toolchain bundle, Python, SCons, compiler/binutils, nanopb generator, and host-tool digests; reject an unpinned or wrong-host toolchain before building.
3. Write failing verifier tests for wrong version, wrong archive digest, missing tool, unexpected host fallback, and altered compiler flags.
4. Run `./fbt f7` twice in separate clean temporary output directories whose absolute paths differ.
5. Separate signed build metadata from reproducible payload bytes; do not normalize an unexplained byte. Compare firmware, updater, resources, API table, and map outputs.
6. Run `python3 tools/verify_toolchain.py` and `python3 tools/check_reproducible_build.py` → Expected: pinned tools and identical reproducible payload digests.
7. Record firmware size, free flash, static RAM, peak heap, boot time, task count, API version, compiler flags, and measurement method in `docs/development/build-baseline.md`.

**Achievement check:** Two independent builds from the same inputs produce the same normalized artifacts.

### Task 7: Create CI and documentation validation

**Files:**
- Create: `.github/workflows/build.yml`
- Create: `.github/workflows/lint.yml`
- Create: `.github/workflows/unit.yml`
- Create: `.github/workflows/provenance.yml`
- Create: `tools/verify_docs.py`
- Test: `tools/tests/test_verify_docs.py`

**Steps:**

1. Write doc tests for tagged code fences, valid repository path citations, unique requirement IDs, and no empty sections.
2. Implement workflows for build, lint, firmware unit image, provenance/SBOM, and documentation checks.
3. Run `./fbt lint_all`, `./fbt f7`, `python3 tools/verify_baseline.py`, and `python3 tools/verify_docs.py`.
4. Expected: all commands exit `0`; workflow YAML parses; generated artifacts are not committed.

**Achievement check:** Every M0 quality gate runs locally and in CI from pinned inputs.

## Achievement A0.4: Security and Hardware Readiness

### Task 8: Establish signing and threat-model governance

**Files:**
- Create: `docs/security/threat-model.md`
- Create: `docs/release/signing-policy.md`
- Create: `docs/release/key-ceremony.md`
- Create: `docs/decisions/ADR-0004-signing-authorities.md`
- Create: `schemas/poison/revocation-manifest.schema.json`
- Create: `tools/signing/verify_manifest.py`
- Test: `tools/tests/test_verify_manifest.py`

**Steps:**

1. Convert SPEC-1 Appendix D threats into assets, trust boundaries, abuse cases, and owners.
2. Decide the signature primitive supported by the device crypto stack and the offline-root/online-intermediate ownership model; document package, content, firmware, provenance, and support-bundle signing scopes.
3. Define rotations, expiry, rollback protection, revocation distribution, emergency authority, threshold/dual-control ceremony, and separation of test/production keys.
4. Implement signature and revocation-manifest verification using test-only fixtures; never create production private keys in the repository.
5. Run `python3 -m unittest tools.tests.test_verify_manifest` → Expected: valid fixture accepted; tampered, expired, wrong-scope, downgraded, and revoked fixtures rejected.

**Achievement check:** Signing policy is testable and no production secret is required for development.

### Task 9: Commission the hardware-in-loop rack

**Files:**
- Create: `tools/hil/README.md`
- Create: `tools/hil/inventory.example.json`
- Create: `tools/hil/run_suite.py`
- Create: `tools/hil/suites/baseline.py`
- Create: `tools/hil/suites/firmware_units.py`
- Test: `tools/tests/test_hil_inventory.py`

**Steps:**

1. Define two physical device roles: test and recovery, plus controllable USB power and known SD fixtures.
2. Validate inventory schema without committing real serials or credentials.
3. Implement baseline flash, boot, version, storage, and recovery checks using existing `scripts/testops.py` interfaces.
4. Implement the `firmware-units` suite as the canonical wrapper around `./fbt FIRMWARE_APP_SET=unit_tests` followed by the existing `python3 scripts/testops.py run_units` physical-device runner.
5. Run `python3 tools/hil/run_suite.py --suite baseline` and `python3 tools/hil/run_suite.py --suite firmware-units` → Expected: both physical roles identified and all baseline/unit checks PASS.

**Achievement check:** A broken firmware candidate can be detected and recovered without manual undocumented steps.

## Achievement A0.5: Shared Protocol and Compatibility Tooling

### Task 10: Implement deterministic C, TypeScript, and Rust protocol generation

**Files:**
- Modify: `assets/SConscript:18-24`
- Modify: `scripts/fbt_tools/fbt_assets.py:22-28,133-184`
- Create: `tools/protocol/package.json`
- Create: `tools/protocol/pnpm-lock.yaml`
- Create: `tools/protocol/Cargo.toml`
- Create: `tools/protocol/Cargo.lock`
- Create: `tools/protocol/generate.py`
- Create: `tools/protocol/check_generated.py`
- Create: `tools/protocol/compatibility.py`
- Create: `schemas/protocol/bounds.yaml`
- Test: `tools/tests/test_protocol_codegen.py`
- Test: `tools/tests/test_protocol_compatibility.py`

**Steps:**

1. Write fixtures that add an optional field, remove/renumber a field, reuse a reserved tag, add an enum value, exceed a nanopb bound, and generate twice in different directories.
2. Keep `assets/protobuf/*.proto` canonical. Generate bounded nanopb C through the existing `ProtoBuilder`, TypeScript through one pinned protoc plugin, and Rust through one pinned prost plugin; generated outputs are never edited manually.
3. Require `schemas/protocol/bounds.yaml` entries for every firmware string, byte field, repeated field, chunk, queue, and maximum encoded message.
4. Make compatibility checks reject field/tag reuse and breaking current/previous-stable changes while allowing unknown optional fields and enum values.
5. Run `python3 tools/protocol/generate.py`, `python3 tools/protocol/check_generated.py`, and `python3 tools/protocol/compatibility.py --against provenance/protocol-previous.json` twice → Expected: byte-identical output and intentional fixture results.
6. Integrate all three checks into `./fbt proto` and CI before any M1 schema is added.

### Task 11: Snapshot firmware API and run official parity tests

**Files:**
- Create: `provenance/firmware-api.lock.csv`
- Create: `provenance/protocol-previous.json`
- Create: `tools/snapshot_firmware_api.py`
- Create: `tools/run_official_parity.py`
- Create: `docs/testing/ofw-parity.md`
- Test: `tools/tests/test_snapshot_firmware_api.py`
- Test: `tools/tests/test_run_official_parity.py`

**Steps:**

1. Snapshot the generated F7 API symbol table with API version, name hash, signature, source owner, and compatibility classification.
2. Run the upstream formatting/build/unit/updater tests that apply to the materialized baseline and record exact upstream commit, command, hardware, and result.
3. Compare PoisonedOS baseline behavior and artifacts with the locked OFW snapshot; classify every expected product-name/build-metadata difference and fail on unexplained differences.
4. Run `python3 tools/snapshot_firmware_api.py --check` and `python3 tools/run_official_parity.py --baseline do_not_include/flipperzero-firmware` → Expected: API snapshot stable and all applicable official tests pass.

## M0 Exit Gate

```bash
python3 -m unittest discover tools/tests
python3 tools/verify_baseline.py
python3 tools/compare_upstream.py --check
python3 tools/verify_toolchain.py
python3 tools/generate_sbom.py --check
python3 tools/verify_docs.py
python3 tools/protocol/check_generated.py
python3 tools/protocol/compatibility.py --against provenance/protocol-previous.json
python3 tools/snapshot_firmware_api.py --check
python3 tools/run_official_parity.py --baseline do_not_include/flipperzero-firmware
./fbt lint_all
./fbt f7
./fbt FIRMWARE_APP_SET=unit_tests
python3 tools/check_reproducible_build.py
python3 tools/hil/run_suite.py --suite baseline
python3 tools/hil/run_suite.py --suite firmware-units
```

Expected: all exit `0`; two normalized builds match; zero unknown licenses; two physical devices pass recovery checks. Update the M0 achievement ledger in the master plan, show all diffs, and ask before any commit.

## Deferred Findings

Record out-of-scope findings here with evidence, impact, and owner. Do not implement them during M0.
