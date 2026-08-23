# Builder Isolation Runbook

## Owner

Builder maintainer; backup is the security maintainer.

## Prerequisites

Use the pinned Rust toolchain, locked vendor inputs, network-disabled policy, empty writable job directory, and bounded source/output/log limits.

## Procedure

```bash
python3 tools/rust/cargo.py test --manifest-path builder/Cargo.toml
python3 tools/rust/verify_vendor.py --locked
```

Quarantine a job on policy violation, preserve its provenance digest, and remove only its isolated temporary workspace after retention rules permit.

## Verification

Confirm network-disabled policy, provenance completeness, artifact admission, bounded logs, and no host credentials or devices are exposed.

## Escalation

Escalate any sandbox escape, dependency mismatch, or unexplained output to security before resuming builds.
