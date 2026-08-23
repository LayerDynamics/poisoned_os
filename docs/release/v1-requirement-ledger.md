# PoisonedOS V1 Requirement Ledger

The authoritative machine-readable ledger is `artifacts/release-evidence/v1-requirements.json`. Run:

```bash
python3 tools/release/verify_spec.py --spec docs/specs/SPEC-1-poisonedos-for-the-flipper-zero.md --evidence artifacts/release-evidence
```

The verifier fails closed when evidence is absent, duplicated, stale, synthetic, failed, or outside the evidence root. This repository currently has incomplete M0–M6 evidence, so a stable-release pass is not asserted.
