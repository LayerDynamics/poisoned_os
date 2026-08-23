# Support Triage Runbook

## Owner

Support maintainer; backup is the diagnostics maintainer.

## Prerequisites

Obtain previewed consent, the support-bundle digest, software versions, and the operator-visible error without requesting raw evidence or secrets.

## Procedure

```bash
python3 tools/security/verify_redaction.py --help
python3 tools/release/evaluate_rollout_health.py --help
```

Classify the issue, verify the digest-only bundle, reproduce locally when possible, and attach only approved redacted material.

## Verification

Confirm consent, redaction, integrity, retention classification, and an actionable owner before closing the case.

## Escalation

Escalate suspected security, evidence-integrity, or unrecoverable-update issues to the security or release maintainer.
